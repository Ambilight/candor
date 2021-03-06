/**
 * Copyright (c) 2012, Fedor Indutny.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "parser.h"

#include <assert.h>  // assert
#include <stdlib.h>  // NULL

#include "ast.h"

namespace candor {
namespace internal {

AstNode* Parser::Execute() {
  AstNode* stmt;
  while ((stmt = ParseStatement(kSkipTrailingCr)) != NULL) {
    ast()->children()->Push(stmt);
  }

  // If parsing was successful - reset any errors
  if (Peek()->is(kEnd)) {
    if (offset_ != length_) {
      SetError("Unexpected symbol");
    } else {
      // Calculate size of main function
      FunctionLiteral* fn = FunctionLiteral::Cast(ast());
      fn->end(Peek()->offset());

      fn->own_length(fn->length());
      while (fns_.length() != 0) {
        FunctionLiteral* i = fns_.Pop();

        // Unroll all visited functions
        fn->own_length(fn->own_length() - i->length());
      }

      SetError(NULL);
    }
  }

  return ast();
}


AstNode* Parser::ParseStatement(ParseStatementType type) {
  Position pos(this);
  AstNode* result = NULL;

  // Skip cr's before statement
  // needed for {\n blocks \n}
  SkipCr();

  switch (Peek()->type()) {
    case kReturn:
      {
        result = Add(Add(new AstNode(AstNode::kReturn, Peek())));
        Skip();
        AstNode* value = ParseExpression();
        if (value == NULL) {
          value = Add(Add(new AstNode(AstNode::kNil)));
          value->value("nil");
          value->length(3);
        }
        result->children()->Push(value);
      }
      break;
    case kContinue:
    case kBreak:
      result = Add(Add(new AstNode(AstNode::ConvertType(Peek()->type()),
              Peek())));
      Skip();
      break;
    case kIf:
      {
        Lexer::Token* if_tok = Peek();

        Skip();
        if (!Peek()->is(kParenOpen)) {
          SetError("Expected '(' before if's condition");
          return NULL;
        }
        Skip();

        AstNode* cond = ParseExpression();
        if (cond == NULL) {
          SetError("Expected if's condition");
          return NULL;
        }

        if (!Peek()->is(kParenClose)) {
          SetError("Expected ')' after if's condition");
          return NULL;
        }
        Skip();

        AstNode* body = ParseBlock(NULL);
        AstNode* elseBody = NULL;

        if (body == NULL) {
          body = ParseStatement(kLeaveTrailingCr);
        } else {
          if (Peek()->is(kElse)) {
            Skip();
            elseBody = ParseBlock(NULL);
            if (elseBody == NULL) {
              elseBody = ParseStatement(kLeaveTrailingCr);
            }

            if (elseBody == NULL) {
              SetError("Expected else's body");
              return NULL;
            }
          }
        }

        if (body == NULL) {
          SetError("Expected if's body");
          return NULL;
        }

        result = Add(new AstNode(AstNode::kIf, if_tok));
        result->children()->Push(cond);
        result->children()->Push(body);
        if (elseBody != NULL) result->children()->Push(elseBody);
      }
      break;
    case kWhile:
      Skip();
      {
        if (!Peek()->is(kParenOpen)) {
          SetError("Expected '(' before while's condition");
          return NULL;
        }
        Skip();

        AstNode* cond = ParseExpression();
        if (cond == NULL) {
          SetError("Expected while's condition");
          return NULL;
        }
        if (!Peek()->is(kParenClose)) {
          SetError("Expected ')' after while's condition");
          return NULL;
        }
        Skip();

        AstNode* body = ParseBlock(NULL);
        if (body == NULL) {
          SetError("Expected while's body");
          return NULL;
        }

        result = Add(new AstNode(AstNode::kWhile));
        result->children()->Push(cond);
        result->children()->Push(body);
      }
      break;
    case kBraceOpen:
      result = ParseBlock(NULL);
      break;
    default:
      result = ParseExpression();
      break;
  }

  // Consume kCr or kBraceClose
  if (!Peek()->is(kEnd) && !Peek()->is(kCr) && !Peek()->is(kBraceClose)) {
    SetError("Expected CR, EOF, or '}' after statement");
    return NULL;
  }
  if (type == kSkipTrailingCr) SkipCr();

  return pos.Commit(result);
}

#define BINOP_PRI1\
    case kLOr:\

#define BINOP_PRI2\
    case kLAnd:\

#define BINOP_PRI3\
    case kEq:\
    case kNe:\
    case kStrictEq:\
    case kStrictNe:

#define BINOP_PRI4\
    case kLt:\
    case kGt:\
    case kLe:\
    case kGe:

#define BINOP_PRI5\
    case kBOr:\
    case kBAnd:\
    case kBXor:\
    case kMod:\
    case kShl:\
    case kShr:\
    case kUShr:

#define BINOP_PRI6\
    case kAdd:\
    case kSub:

#define BINOP_PRI7\
    case kMul:\
    case kDiv:

#define BINOP_SWITCH(type, result, priority, K)\
    type = Peek()->type();\
    switch (type) {\
     K\
      result = ParseBinOp(type, result, priority);\
     default:\
      break;\
    }\
    if (result == NULL) {\
      SetError("Failed to parse binary operation");\
      return result;\
    }


AstNode* Parser::ParseExpression(int priority) {
  AstNode* result = NULL;
  AstNode* member = NULL;

  Position pos(this);

  // Parse prefix unops and block expression
  switch (Peek()->type()) {
    case kInc:
    case kDec:
    case kNot:
    case kAdd:
    case kSub:
      member = ParsePrefixUnOp(Peek()->type());
      break;
    case kTypeof:
    case kSizeof:
    case kKeysof:
    case kClone:
    case kDelete:
      {
        Position pos(this);

        TokenType type = Peek()->type();
        Skip();

        AstNode* expr = ParseExpression(7);
        if (expr == NULL) {
          SetError("Expected body of prefix operation");
          return NULL;
        }

        member = Add(new AstNode(AstNode::ConvertType(type)));
        member->children()->Push(expr);

        pos.Commit(member);
        break;
      }
    default:
      member = ParseMember();
      break;
  }

  switch (Peek()->type()) {
    case kAssign:
      if (member == NULL) {
        SetError("Expected lhs before '='");
        return NULL;
      }

      if (member->type() != AstNode::kName &&
          member->type() != AstNode::kMember) {
        SetError("Invalid lhs");
        return NULL;
      }

      // member "=" expr
      {
        Lexer::Token* token = Peek();
        Skip();
        AstNode* value = ParseExpression();
        if (value == NULL) {
          SetError("Expected rhs after '='");
          return NULL;
        }
        result = Add(new AstNode(AstNode::kAssign, token));
        result->children()->Push(member);
        result->children()->Push(value);
      }
      break;
    default:
      result = member;
      break;
  }

  if (result == NULL) {
    return result;
  }

  // Parse postfixes
  TokenType type = Peek()->type();
  switch (type) {
    case kInc:
      Skip();
      result = Add(new UnOp(UnOp::kPostInc, result));
      break;
    case kDec:
      Skip();
      result = Add(new UnOp(UnOp::kPostDec, result));
      break;
    case kEllipsis:
      {
        Skip();
        AstNode* varg = Add(new AstNode(AstNode::kVarArg, result));
        varg->children()->Push(result);
        result = varg;
      }
      break;
    default:
      break;
  }

  // Parse binops ordered by priority
  AstNode* initial;
  do {
    initial = result;
    switch (priority) {
      default:
      case 1:
        BINOP_SWITCH(type, result, 1, BINOP_PRI1)
      case 2:
        BINOP_SWITCH(type, result, 2, BINOP_PRI2)
      case 3:
        BINOP_SWITCH(type, result, 3, BINOP_PRI3)
      case 4:
        BINOP_SWITCH(type, result, 4, BINOP_PRI4)
      case 5:
        BINOP_SWITCH(type, result, 5, BINOP_PRI5)
      case 6:
        BINOP_SWITCH(type, result, 6, BINOP_PRI6)
      case 7:
        BINOP_SWITCH(type, result, 7, BINOP_PRI7)
      case 8:
        break;
        // Do not parse binary operations
    }
  } while (initial != result);

  return pos.Commit(result);
}


AstNode* Parser::ParsePrefixUnOp(TokenType type) {
  Position pos(this);

  // Consume prefix token
  Skip();

  AstNode* expr;
  {
    NegateSign n(this, type);

    expr = ParseExpression(8);
  }

  if (expr == NULL) {
    SetError("Expected expression after unary operation");
    return NULL;
  }

  return pos.Commit(Add(new UnOp(UnOp::ConvertPrefixType(NegateType(type)),
                                 expr)));
}


AstNode* Parser::ParseBinOp(TokenType type, AstNode* lhs, int priority) {
  Position pos(this);

  // Consume binop token
  Skip();

  SkipCr();

  AstNode* rhs;
  {
    NegateSign n(this, type);

    rhs = ParseExpression(priority);
  }

  if (rhs == NULL) {
    SetError("Expected rhs for binary operation");
    return NULL;
  }

  AstNode* result = Add(new BinOp(BinOp::ConvertType(NegateType(type)),
                                  lhs,
                                  rhs));

  return pos.Commit(result);
}


AstNode* Parser::ParsePrimary(PrimaryRestriction rest) {
  Position pos(this);
  Lexer::Token* token = Peek();
  AstNode* result = NULL;

  switch (token->type()) {
    case kName:
    case kNumber:
    case kString:
    case kTrue:
    case kFalse:
    case kNil:
      result = Add(new AstNode(AstNode::ConvertType(token->type()), token));
      Skip();
      break;
    case kParenOpen:
      Skip();
      result = ParseExpression();
      if (!Peek()->is(kParenClose)) {
        SetError("Expected closing paren for primary expression");
        return NULL;
      } else {
        Skip();
      }

      // Check if we haven't parsed function's declaration by occasion
      if (Peek()->is(kBraceOpen)) {
        SetError("Unexpected '{' after expression in parens");
        return NULL;
      }
      break;
    case kReturn:
    case kBreak:
    case kContinue:
    case kClone:
    case kTypeof:
    case kSizeof:
    case kKeysof:
      if (rest != kNoKeywords) {
        result = Add(new AstNode(AstNode::ConvertType(token->type()), token));
        Skip();
        break;
      }
    default:
      result = NULL;
      break;
  }

  return pos.Commit(result);
}


AstNode* Parser::ParseMember() {
  Position pos(this);
  AstNode* result = ParsePrimary(kNoKeywords);

  bool colon_call = false;
  while (!Peek()->is(kEnd) && !Peek()->is(kCr)) {
    if (colon_call && !Peek()->is(kParenOpen)) {
      SetError("Expected '(' after colon call");
      return NULL;
    }

    if (Peek()->is(kParenOpen)) {
      // Calls and function declarations
      FunctionLiteral* fn = new FunctionLiteral(result);
      Add(fn);
      result = NULL;

      // Always set start offset for function
      if (result == NULL) fn->offset(Peek()->offset());
      Skip();

      // Push function to the list
      fns_.Push(fn);

      if (colon_call) {
        fn->args()->Push(Add(new AstNode(AstNode::kSelf)));
        colon_call = false;
      }

      SkipCr();
      while (!Peek()->is(kParenClose) && !Peek()->is(kEnd)) {
        AstNode* expr = ParseExpression();
        if (expr == NULL) break;
        fn->args()->Push(expr);

        SkipCr();

        // Skip commas
        if (!Peek()->is(kComma)) {
          SetError("Failed to parse function's arguments");
          break;
        }

        Skip();
        SkipCr();
      }
      if (!Peek()->is(kParenClose)) {
        SetError("Failed to parse function's arguments");
        break;
      }
      Skip();

      // Optional body (for function declaration)
      ParseBlock(reinterpret_cast<AstNode*>(fn));
      if (!fn->CheckDeclaration()) {
        SetError("Incorrect function declaration or call");
        break;
      }

      fn->end(Peek()->offset());

      fn->own_length(fn->length());
      while (fns_.tail()->value() != fn) {
        FunctionLiteral* i = fns_.Pop();

        // Unroll all visited functions
        fn->own_length(fn->own_length() - i->length());
      }

      result = fn;
    } else {
      if (result == NULL) {
        if (Peek()->is(kBraceOpen)) {
          result = ParseObjectLiteral();
        } else if (Peek()->is(kArrayOpen)) {
          result = ParseArrayLiteral();
        }
      }

      if (result == NULL) {
        SetError("Expected expression or statement");
        break;
      }

      AstNode* next = NULL;
      Lexer::Token* token = Peek();
      switch (token->type()) {
       case kColon:
        if (colon_call != false) {
          SetError("Nested colons in method invocation are not supported");
          return NULL;
        }
        colon_call = true;
       case kDot:
        // a.b || a:b(args)
        Skip();
        next = ParsePrimary(kAny);
        if (next != NULL && !next->is(AstNode::kName)) {
          SetError("Expression after '.' ain't allowed!");
          return NULL;
        }
        next->type(AstNode::kProperty);
        break;
       case kArrayOpen:
        // a["prop-expr"]
        Skip();
        next = ParseExpression();
        if (Peek()->is(kArrayClose)) {
          Skip();
        } else {
          next = NULL;
        }
        break;
       default:
        break;
      }

      if (next == NULL) break;

      AstNode* node = Add(new AstNode(AstNode::kMember, token));
      node->children()->Push(result);
      node->children()->Push(next);

      result = node;
    }
  }

  if (colon_call) {
    SetError("Expected '(' after colon call");
    return NULL;
  }

  return pos.Commit(result);
}


AstNode* Parser::ParseObjectLiteral() {
  Position pos(this);

  // Skip '{'
  if (!Peek()->is(kBraceOpen)) {
    SetError("Expected '{'");
    return NULL;
  }
  Skip();

  ObjectLiteral* result = new ObjectLiteral();
  Add(result);

  while (!Peek()->is(kBraceClose) && !Peek()->is(kEnd)) {
    AstNode* key;
    SkipCr();
    switch (Peek()->type()) {
     case kString:
     case kName:
     case kTypeof:
     case kSizeof:
     case kKeysof:
     case kClone:
     case kDelete:
      key = Add(new AstNode(AstNode::kProperty, Peek()));
      Skip();
      break;
     case kNumber:
      key = Add(new AstNode(AstNode::kNumber, Peek()));
      Skip();
      break;
     default:
      SetError("Expected string or number as object literal's key");
      return NULL;
    }

    // Skip ':'
    if (!Peek()->is(kColon)) {
      SetError("Expected colon after object literal's key");
      return NULL;
    }
    Skip();

    // Parse expression
    AstNode* value = ParseExpression();
    if (value == NULL) {
      SetError("Expected expression after colon");
      return NULL;
    }

    result->keys()->Push(key);
    result->values()->Push(value);

    // Skip ',' or exit loop on '}'
    if (Peek()->is(kComma)) {
      Skip();
    } else {
      SkipCr();
      if (!Peek()->is(kBraceClose)) {
        SetError("Expected '}' or ','");
        return NULL;
      }
    }
    SkipCr();
  }

  // Skip '}'
  if (!Peek()->is(kBraceClose)) {
    SetError("Expected '}'");
    return NULL;
  }
  Skip();

  return pos.Commit(result);
}


AstNode* Parser::ParseArrayLiteral() {
  Position pos(this);

  // Skip '['
  if (!Peek()->is(kArrayOpen)) {
    SetError("Expected '['");
    return NULL;
  }

  AstNode* result = Add(new AstNode(AstNode::kArrayLiteral, Peek()));
  Skip();

  while (!Peek()->is(kArrayClose) && !Peek()->is(kEnd)) {
    SkipCr();
    // Parse expression
    AstNode* value = ParseExpression();
    if (value == NULL) {
      SetError("Expected expression after array literal's start");
      return NULL;
    }

    result->children()->Push(value);
    SkipCr();

    // Skip ',' or exit loop on ']'
    if (Peek()->is(kComma)) {
      Skip();
    } else if (!Peek()->is(kArrayClose)) {
      SetError("Expected ']' or ','");
      return NULL;
    }

    SkipCr();
  }

  // Skip ']'
  if (!Peek()->is(kArrayClose)) {
    SetError("Expected ']'");
    return NULL;
  }
  Skip();

  return pos.Commit(result);
}


AstNode* Parser::ParseBlock(AstNode* block) {
  if (!Peek()->is(kBraceOpen)) {
    SetError("Expected '{'");
    return NULL;
  }

  bool fn = block != NULL;

  Position pos(this);

  AstNode* result = fn ?
      block
      :
      Add(new AstNode(AstNode::kBlock, Peek()));
  Skip();

  while (!Peek()->is(kEnd) && !Peek()->is(kBraceClose)) {
    AstNode* stmt = ParseStatement(kSkipTrailingCr);
    if (stmt == NULL) {
      SetError("Expected statement after '{'");
      break;
    }
    result->children()->Push(stmt);
  }
  if (!Peek()->is(kBraceClose)) {
    SetError("Expected '}'");
    return NULL;
  }
  Skip();

  // Block should not be empty
  if (result->children()->length() == 0) {
    result->children()->Push(Add(new AstNode(AstNode::kNop)));
  }

  return pos.Commit(result);
}


void Parser::Print(char* buffer, uint32_t size) {
  PrintBuffer p(buffer, size);
  ast()->PrintChildren(&p, ast()->children());
  p.Finalize();
}

}  // namespace internal
}  // namespace candor
