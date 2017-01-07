#include "smmgvpass.h"

#include <assert.h>

static uint32_t id(void* n) {
	return ((uintptr_t)n) & 0xffff;
}

static const char* typeName(PSmmTypeInfo type) {
	if (type) return type->name;
	return "noType";
}

static void printNode(void* n, const char* label, const char* color, FILE* f) {
	if (color == NULL) {
		fprintf(f, "  n%.5x [label=\"%s\"];\n", id(n), label);
	} else {
		fprintf(f, "  n%.5x [label=\"%s\", fillcolor=\"%s\"];\n", id(n), label, color);
	}
}

static void printEdge(void* n1, void* n2, const char* compass, FILE* f) {
	fprintf(f, "  n%.5x:%s -> n%.5x;\n", id(n1), compass, id(n2));
}

static void printNodeConn(void* n1, void *n2, const char* label, const char* compass, FILE* f) {
	printNode(n2, label, NULL, f);
	printEdge(n1, n2, compass, f);
}

static void printColorNodeConn(void* n1, void *n2, const char* label, const char* color, const char* compass, FILE* f) {
	printNode(n2, label, color, f);
	printEdge(n1, n2, compass, f);
}

static void processExpression(void* parent, PSmmAstNode expr, const char* pcompass, FILE* f) {

	switch (expr->kind) {
	case nkSmmAdd: case nkSmmFAdd: case nkSmmSub: case nkSmmFSub:
	case nkSmmMul: case nkSmmFMul: case nkSmmUDiv: case nkSmmSDiv: case nkSmmFDiv:
	case nkSmmURem: case nkSmmSRem: case nkSmmFRem:
	case nkSmmAndOp: case nkSmmOrOp:
	case nkSmmXorOp:
	case nkSmmEq: case nkSmmNotEq: case nkSmmGt: case nkSmmGtEq: case nkSmmLt: case nkSmmLtEq:
		{
			processExpression(expr, expr->right, "se", f);
			//fallthrough
		}
	case nkSmmNeg: case nkSmmNot: case nkSmmCast:
		{
			char buf[100] = { 0 };
			sprintf(buf, "%s: %s", nodeKindToString[expr->kind], typeName(expr->type));
			if (pcompass[0] == 's' && pcompass[1] == 0) {
				printColorNodeConn(parent, expr, buf, "palegreen", pcompass, f);
			} else {
				printNodeConn(parent, expr, buf, pcompass, f);
			}
			processExpression(expr, expr->left, "sw", f);
			break;
		}
	case nkSmmCall:
		{
			char buf[100] = { 0 };
			sprintf(buf, "call %s: %s", expr->token->repr, typeName(expr->type));
			if (pcompass[0] == 's' && pcompass[1] == 0) {
				printColorNodeConn(parent, expr, buf, "palegreen", pcompass, f);
			} else {
				printNodeConn(parent, expr, buf, pcompass, f);
			}
			PSmmAstCallNode callNode = (PSmmAstCallNode)expr;
			if (callNode->args) {
				void* prevArg = callNode;
				PSmmAstNode astArg = callNode->args;
				while (astArg) {
					processExpression(prevArg, astArg, "se", f);
					prevArg = astArg;
					astArg = astArg->next;
				}
			}
			break;
		}
	case nkSmmParam: case nkSmmIdent: case nkSmmConst:
	case nkSmmInt: case nkSmmFloat: case nkSmmBool:
		{
			char buf[100] = { 0 };
			sprintf(buf, "%s: %s", expr->token->repr, typeName(expr->type));
			if (pcompass[0] == 's' && pcompass[1] == 0) {
				printColorNodeConn(parent, expr, buf, "palegreen", pcompass, f);
			} else {
				printNodeConn(parent, expr, buf, pcompass, f);
			}
			break;
		}
	default:
		assert(false && "Got unexpected node type in processExpression");
		break;
	}
}

static void processAssignment(void* parent, PSmmAstNode stmt, FILE* f) {
	char buf[100] = { 0 };
	sprintf(buf, "= %s", typeName(stmt->type));
	if (((PSmmAstNode)parent)->kind == nkSmmDecl) {
		printNodeConn(parent, stmt, buf, "se", f);
	} else {
		printColorNodeConn(parent, stmt, buf, "palegreen", "s", f);
	}
	sprintf(buf, "%s: %s", stmt->left->token->repr, typeName(stmt->left->type));
	printNodeConn(stmt, stmt->left, buf, "sw", f);
	processExpression(stmt, stmt->right, "se", f);
}

static void processLocalSymbols(PSmmAstScopeNode scope, FILE* f) {
	void* prevDecl = scope;
	PSmmAstDeclNode decl = scope->decls;
	while (decl) {
		printNodeConn(prevDecl, decl, "decl", "s", f);
		if (decl->left->kind == nkSmmAssignment) {
			if (decl->left->left->isConst) {
				processAssignment(decl, decl->left, f);
			} else {
				printEdge(decl, decl->left->left, "se", f);
			}
		} else {
			assert(false && "Declaration of unknown node kind");
		}
		prevDecl = decl;
		decl = decl->nextDecl;
	}
}

static void processReturn(void* parent, PSmmAstNode stmt, FILE* f) {
	char buf[100] = { 0 };
	sprintf(buf, "return: %s", typeName(stmt->type));
	printColorNodeConn(parent, stmt, buf, "palegreen", "s", f);
	if (stmt->left)	processExpression(stmt, stmt->left, "sw", f);
}

static void processBlock(PSmmAstBlockNode block, FILE* f) {
	void* prevStmt = block;
	PSmmAstNode stmt = block->stmts;
	while (stmt) {
		switch (stmt->kind) {
		case nkSmmBlock:
			{
				printColorNodeConn(prevStmt, stmt, "block", "palegreen", "s", f);
				PSmmAstBlockNode newBlock = (PSmmAstBlockNode)stmt;
				printNodeConn(newBlock, newBlock->scope, "scope", "sw", f);
				processLocalSymbols(newBlock->scope, f);
				processBlock(newBlock, f);
				prevStmt = stmt;
				break;
			}
		case nkSmmAssignment:
			processAssignment(prevStmt, stmt, f);
			prevStmt = stmt;
			break;
		case nkSmmReturn:
			processReturn(prevStmt, stmt, f);
			prevStmt = stmt;
			break;
		case nkSmmDecl:
			if (stmt->left->left->isConst) {
				assert(false && "Const declaration should not appear as statements");
			} else {
				// Var declarations should appear as statements because initial value needs to be assigned
				processAssignment(prevStmt, stmt->left, f);
			}
			prevStmt = stmt->left;
			break;
		default:
			processExpression(prevStmt, stmt, "s", f);
			prevStmt = stmt;
			break;
		}
		stmt = stmt->next;
	}
}

static void processGlobalSymbols(PSmmAstScopeNode scope, FILE* f) {
	PSmmAstDeclNode prevDecl = (PSmmAstDeclNode)scope;
	PSmmAstDeclNode decl = scope->decls;
	while (decl) {
		printNodeConn(prevDecl, decl, "decl", "s", f);
		if (decl->left->kind == nkSmmFunc) {
			PSmmAstFuncDefNode funcNode = (PSmmAstFuncDefNode)decl->left;
			char buf[200] = { 0 };
			sprintf(buf, "func %s -> %s", funcNode->token->repr, funcNode->returnType->name);
			printNodeConn(decl, funcNode, buf, "sw", f);
			void * prevParam = funcNode;
			PSmmAstParamNode curParam = funcNode->params;
			while (curParam) {
				sprintf(buf, "%s: %s", curParam->token->repr, typeName(curParam->type));
				printNodeConn(prevParam, curParam, buf, "sw", f);
				prevParam = curParam;
				curParam = curParam->next;
			}
			if (funcNode->body) {
				printColorNodeConn(funcNode, funcNode->body, "funcBody", "palegreen", "s", f);
				printNodeConn(funcNode->body, funcNode->body->scope, "scope", "sw", f);
				processLocalSymbols(funcNode->body->scope, f);
				processBlock(funcNode->body, f);
			}
		} else if (decl->left->left->isConst) {
			processAssignment(decl, decl->left, f);
			assert(decl->left->right && "Global var must have initializer");
		} else {
			assert(decl->left->right && "Global var must have initializer");

			printNodeConn(decl, decl->left->left, decl->left->left->token->repr, "se", f);
		}
		prevDecl = decl;
		decl = decl->nextDecl;
	}
}

void smmExecuteGVPass(PSmmAstNode module, FILE* f) {
	const char* moduleName = module->token->repr;
	fprintf(f, "digraph \"%s\" {\n  node [ style = filled ]\n", moduleName);
	printNode(module, moduleName, "mediumaquamarine", f);
	PSmmAstBlockNode globalBlock = (PSmmAstBlockNode)module->next;
	printColorNodeConn(module, globalBlock, "globalBlock", "palegreen", "s", f);
	assert(globalBlock->kind == nkSmmBlock);
	printNodeConn(globalBlock, globalBlock->scope, "globalScope", "sw", f);
	processGlobalSymbols(globalBlock->scope, f);

	processBlock(globalBlock, f);
	fputs("}\n", f);
}
