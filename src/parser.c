#include "includes/parser.h"

// creates a new empty scope
Scope *newScope(Scope *outer) {
	Scope *scope = (Scope *)malloc(sizeof(Scope));
	scope->outer = outer;
	ScopeObject *objects = NULL;
	scope->objects = objects;

	return scope;
}

// NewParser creates a new parser
Parser *NewParser(char *src, Token *tokens) {
	Parser *parser = (Parser *)malloc(sizeof(Parser));
	parser->src = src;
	parser->tokens = tokens;
	parser->scope = newScope(NULL);
	parser->expLevel = 0;
	parser->rhs = false;
	
	return parser;
}

// EnterScope enters a new inner scope
void EnterScope(Parser *parser) {
	parser->scope = newScope(parser->scope);
}

// ExitScope exits the current scope
void ExitScope(Parser *parser) {
	// clear hash table and free all scope objects
	ScopeObject *obj, *tmp;
	HASH_ITER(hh, parser->scope->objects, obj, tmp) {
		HASH_DEL(parser->scope->objects, obj);
		free(obj);
	}

	// Move to outer scipe
	Scope *outer = parser->scope->outer;
	free(parser->scope);
	parser->scope = outer;
}

// InsertScope inserts an object into the current scope
bool InsertScope(Parser *parser, char *name, Object *object) {
	// check if name is already in scope
	ScopeObject *obj;
	HASH_FIND_STR(parser->scope->objects, name, obj);
	if (obj != NULL) return false;

	// add object to scope
	obj = (ScopeObject *)malloc(sizeof(ScopeObject));
	obj->name = name;
	obj->obj = object;
	HASH_ADD_KEYPTR(hh, parser->scope->objects, obj->name, strlen(obj->name), obj);
	return true;
}

// FindScope finds an object in scope
Object *FindScope(Parser *parser, char *name) {
	ScopeObject *obj;
	for (Scope *scope = parser->scope; scope != NULL; scope = scope->outer) {
		HASH_FIND_STR(scope->objects, name, obj);
		if (obj != NULL) return obj->obj;
	}

	return NULL;
}

// BindingPower returns the left binding power of a token
int BindingPower(TokenType type) {
	switch (type) {
	case END:
		return -10;
	// Non-binding operators
	case SEMI:
		return 0;
	// Assignment operators
	case ASSIGN:
	case ADD_ASSIGN:
	case SUB_ASSIGN:
	case MUL_ASSIGN:
	case REM_ASSIGN:
	case OR_ASSIGN:
	case SHR_ASSIGN:
	case DEFINE:
		return 10;
	// Logical operators
	case LAND:
	case LOR:
		return 20;
	// Equality operators
	case EQL:
	case NEQ:
	case LSS:
	case GTR:
	case LEQ:
	case GEQ:
		return 30;
	// Math operators
	case ADD:
	case SUB:
		return 40;
	case MUL:
	case QUO:
	case REM:
		return 50;
	// Special unary
	case NOT:
		return 60;
	// Strongly bound
	case PERIOD:
	case LBRACK:
	case LPAREN:
		return 70;
	// Unknow token
	default:
		return 0;
	}
}

// ParserNext moves the parser onto the next token
void ParserNext(Parser *parser) {
	parser->tokens++;
}

// expect asserts that the token is of type type, if true parser advances
void expect(Parser *parser, TokenType type) {
	// TODO: create TokenType -> string
	ASSERT(parser->tokens->type == type, "Expect failed");
	ParserNext(parser);
}

// expectSemi expects a semicolon
void expectSemi(Parser *parser) {
	ASSERT(parser->tokens->type == SEMI || 
		parser->tokens->type == END, "Expected semi");
	ParserNext(parser);
}

// Parses key value expressions in the form "expression:expression" or "expression"
Exp *ParseKeyValueExp(Parser *parser) {
	Exp *keyOrVal = ParseExpression(parser, 0);
	Exp *key = NULL;
	Exp *value = NULL;
	
	if (parser->tokens->type == COLON) {
		// Key/value belongs to structure expression
		parser->tokens++;
		key = keyOrVal;
		value = ParseExpression(parser, 0);
	} else {
		// Key/value belongs to array expression
		value = keyOrVal;
	}

	return newKeyValueExp(key, value);
}

Exp *ParseKeyValueListExp(Parser *parser) {
	int keyCount = 0;
	Exp *values = malloc(0);

	while(parser->tokens->type != RBRACE) {
		keyCount++;
		values = realloc(values, keyCount * sizeof(Exp));
		Exp *keyValue = ParseKeyValueExp(parser);
		memcpy(values + keyCount - 1, keyValue, sizeof(Exp));
		
		if(parser->tokens->type != RBRACE) expect(parser, COMMA);
	}

	return newKeyValueListExp(values, keyCount);
}

Exp *ParseArrayExp(Parser *parser) {
	int valueCount = 0;
	Exp *values = malloc(0);
	while(parser->tokens->type != RBRACK) {
		values = realloc(values, (++valueCount) * sizeof(Exp));
		Exp *value = ParseExpression(parser, 0);
		memcpy(values + valueCount - 1, value, sizeof(Exp));
		if (parser->tokens->type != RBRACK) expect(parser, COMMA);
	}

	expect(parser, RBRACK);

	return newArrayExp(values, valueCount);
}

// nud parses the current token in a prefix context (at the start of an (sub)expression)
Exp *nud(Parser *parser, Token *token) {
	switch (token->type) {
	case IDENT:
		return ParseIdentToken(parser, token);
	
	case INT:
	case FLOAT:
	case HEX:
	case OCTAL:
	case STRING:
		return newLiteralExp(*token);

	case NOT:
	case SUB:
		return newUnaryExp(*token, ParseExpression(parser, 60));

	case LBRACE:
		return ParseKeyValueListExp(parser);

	case LBRACK:
		return ParseArrayExp(parser);

	default:
		ASSERT(false, "Expected a prefix token");
	}

	return NULL;
}

// led parses the current token in a infix contect (between two nodes)
Exp *led(Parser *parser, Token *token, Exp *exp) {
	int bp = BindingPower(token->type);
	
	switch (token->type) {
		// binary expression
		case ADD:
		case SUB:
		case MUL:
		case QUO:
		case REM:
		case EQL:
		case NEQ:
		case GTR:
		case LSS:
		case GEQ:
		case LEQ: {
			return newBinaryExp(exp, *token, ParseExpression(parser, bp));
		}

		// selector expression
		case PERIOD: {
			return newSelectorExp(exp, ParseExpression(parser, bp));
		}

		// index expression
		case LBRACK: {
			Exp *index = ParseExpression(parser, 0);
			expect(parser, RBRACK);
			
			return newIndexExp(exp, index);
		}

		// array/struct expression
		case LBRACE: {
			printf("LBrace!");
		}

		// call expression
		case LPAREN: {
			int argCount = 0;
			Exp *args = (Exp *)malloc(0);
			if(parser->tokens->type != RPAREN) {
				// arguments are not empty so parse arguments
				while(true) {
					argCount++;
					
					args = realloc(args, argCount * sizeof(Exp));
					Exp *arg = ParseExpression(parser, 0);
					memcpy(args + argCount - 1, arg, sizeof(Exp));
					
					if(parser->tokens->type == RPAREN) break;
					expect(parser, COMMA);
				}
			}
			expect(parser, RPAREN);

			return newCallExp(exp, args, argCount);
		}

		// right associative binary expression or assignments
		// if the expression is an assigment, return a binary statement and let
		// ParseStatment transform it into a statment.
		case LAND:
		case LOR:
		case ASSIGN:
		case ADD_ASSIGN:
		case SUB_ASSIGN:
		case MUL_ASSIGN:
		case REM_ASSIGN:
		case OR_ASSIGN:
		case SHL_ASSIGN: 
		case DEFINE: {
			return newBinaryExp(exp, *token, ParseExpression(parser, bp - 1));	
		}
		default:
			ASSERT(false, "Expected an infix expression");
	}

	return NULL;
}

Exp *ParseType(Parser *parser) {
	Exp *ident = ParseIdent(parser);
	if(parser->tokens->type == LBRACK) {
		// Type is an array type
		parser->tokens++;
		Exp *length = ParseExpression(parser, 0);
		expect(parser, RBRACK);
		return newArrayTypeExp(ident, length);
	}

	return ident;
}

// smtd parser the current token in the context of the start of a statement
Smt *smtd(Parser *parser, Token *token) {
	switch(token->type) {
		// return statement
		case RETURN: {
			parser->tokens++;
			Smt *s = newReturnSmt(ParseExpression(parser, 0));
			return s; 
		}
		// block statement
		case LBRACE: {
			parser->tokens++;
			
			EnterScope(parser);

			int smtCount = 0;
			Smt *smts = (Smt *)malloc(sizeof(Smt) * 1024);
			Smt *smtsPrt = smts;
			while(parser->tokens->type != RBRACE) {
				smtCount++;
				memcpy(smtsPrt, ParseStatement(parser), sizeof(Smt));
				if(parser->tokens->type != RBRACE) expectSemi(parser);
				smtsPrt++;
			}
			smts = realloc(smts, sizeof(Smt) * smtCount);

			Smt *s = newBlockSmt(smts, smtCount);

			expect(parser, RBRACE);

			ExitScope(parser);

			return s;
		}
		// if statement
		case IF: {
			parser->tokens++;
			
			Exp *cond = ParseExpression(parser, 0);
			Smt *block = ParseStatement(parser);
			ASSERT(block->type == blockSmt, "Expected block after if condition");
			Smt *elses = NULL;

			// Check for elseif/else
			if (parser->tokens->type == ELSE) {
				parser->tokens++;
				if (parser->tokens->type == IF) {
					// else if, so recursivly parse else chain
					elses = ParseStatement(parser);
				} else {
					// final else statment only has a body
					elses = newIfSmt(NULL, ParseStatement(parser), NULL);
				}
			}

			return newIfSmt(cond, block, elses);
		}
		// for loop
		case FOR: {
			parser->tokens++;

			// parse index
			Dcl *index = ParseDeclaration(parser);
			ASSERT(index->type == varibleDcl, "Expected index of for loop to be a varible declaration");
			expectSemi(parser);

			// parse condition
			Exp *cond = ParseExpression(parser, 0);
			expectSemi(parser);

			// parse increment
			Smt *inc = ParseStatement(parser);
			
			// parse body
			Smt *body = ParseStatement(parser);
			ASSERT(body->type == blockSmt, "Expected block statement");

			return newForSmt(index, cond, inc, body);
		}
		// varible declaration
		case VAR: {
			return newDeclareSmt(ParseVar(parser));
		}
		// increment expression
		case IDENT: {
			Exp *ident = ParseIdent(parser);

			switch(parser->tokens->type) {
				case INC:
					parser->tokens++;
					return newBinaryAssignmentSmt(ident, ADD_ASSIGN, newIntLiteral("1"));
				case DEC:
					parser->tokens++;
					return newBinaryAssignmentSmt(ident, SUB_ASSIGN, newIntLiteral("1"));
				default:
					// expression is assigment or declaration so let caller handle it
					parser->tokens--; // go back to ident
					return NULL;
			}
		}
		default:
			ASSERT(false, "Expected a statement");
	}
	return NULL;
}

Exp *ParseIdentToken(Parser *parser, Token *token) {
	ASSERT(token->type == IDENT,
		"Expected identifier");
	
	char *name = token->value;
	Exp *ident = newIdentExp(name);
	
	Object *obj = FindScope(parser, name);
	ident->node.ident.obj = obj;
	return ident;
}

Exp *ParseIdent(Parser *parser) {
	Exp *ident = ParseIdentToken(parser, parser->tokens);
	ParserNext(parser);
	return ident;
}

Dcl *ParseVar(Parser *parser) {
	Exp *name;
	Exp *type = NULL;
	Exp *value;

	if(parser->tokens->type == VAR) {
		parser->tokens++;
		type = ParseType(parser);
		name = ParseIdent(parser);
		expect(parser, ASSIGN);
		value = ParseExpression(parser, 0);
	} else {
		name = ParseIdent(parser);
		expect(parser, DEFINE);
		value = ParseExpression(parser, 0);
	}

	Dcl *dcl = newVaribleDcl(name, type, value);

	char *objName = name->node.ident.name;
	Object *obj = (Object *)malloc(sizeof(Object));
	obj->name = objName;
	obj->node = dcl;
	obj->type = varObj;
	InsertScope(parser, objName, obj);

	return dcl;
}

Dcl *ParseFunction(Parser *parser) {
	expect(parser, PROC);
	Exp *name = ParseIdent(parser); // function name
	expect(parser, DOUBLE_COLON);

	// parse arguments
	Dcl *args = (Dcl *)malloc(0);
	int argCount = 0;
	while(parser->tokens->type != ARROW) {
		if (argCount > 0) expect(parser, COMMA);
		args = realloc(args, sizeof(Dcl) * ++argCount);

		// Construct argument
		Exp *type = ParseType(parser); // arg type
		Exp *name = ParseIdent(parser); // arg name
		Dcl *arg = newArgumentDcl(type, name);
		void *dest = memcpy(args + argCount - 1, arg, sizeof(Dcl));
		
	}
	// insert arguments into scope
	for (int i = 0; i < argCount; i++) {
		// insert into scope
		Object *obj = (Object *)malloc(sizeof(Object));
		obj->name = args[i].node.argument.name->node.ident.name;
		obj->node = args + i;
		obj->type = argObj;
		InsertScope(parser, obj->name, obj);
	}
	expect(parser, ARROW);
	Exp *returnType = ParseType(parser);

	// insert function into scope
	Dcl* function = newFunctionDcl(name, args, argCount, returnType, NULL);
	Object *obj = (Object *)malloc(sizeof(Object));
	obj->name = name->node.ident.name;
	obj->node = function;
	obj->type = funcObj;
	InsertScope(parser, name->node.ident.name, obj);
	
	// parse body
	Smt *body = ParseStatement(parser);
	function->node.function.body = body;

	if(parser->tokens->type == SEMI) parser->tokens++;

	return function;
}

Dcl *ParseDeclaration(Parser *parser) {
	switch(parser->tokens->type) {
		case PROC:
			return ParseFunction(parser);
		case VAR:
		case IDENT:
			return ParseVar(parser);
		default:
			ASSERT(false, "Expected a top level declaration");
	}
}

// Parses the next statement by calling smtd on the first token else handle
// the declaration/assignment
Smt *ParseStatement(Parser *parser) {
	Token *t = parser->tokens;
	Smt *smt = smtd(parser, t);
	if (smt != NULL) {
		return smt;
	}

	// Statement is an assignment/declaration, so treat it like an expression
	// and transform it.
	Exp *exp = ParseExpression(parser, 0);
	ASSERT(exp->type == binaryExp, "Expecting assigment/declation statement");
	
	Exp *left = exp->node.binary.left;
	Exp *right = exp->node.binary.right;
	Token op = exp->node.binary.op;

	switch(op.type) {
		case ASSIGN:
		case ADD_ASSIGN:
		case SUB_ASSIGN:
		case MUL_ASSIGN:
		case REM_ASSIGN:
		case OR_ASSIGN:
		case SHL_ASSIGN:
			smt = newBinaryAssignmentSmt(left, op.type, right);
			break;
		case DEFINE:
			ASSERT(left->type == identExp, 
				"Expected left hand side of declare to be identifier");

			smt = newDeclareSmt(newVaribleDcl(left, NULL, right));
			
			// Added declaration to scope
			char *name = left->node.ident.name;
			Object *obj =(Object *)malloc(sizeof(Object));
			obj->name = name;
			obj->node = smt->node.declare;
			obj->type = varObj;
			InsertScope(parser, name, obj);
			break;
		default:
			ASSERT(false, "Expected an assignment operator");
	}

	// If statment is null, the next tokens dont start a valid statement
	ASSERT(smt != NULL, "Expecting assigment/declation statement");

	free(exp);
	return smt;
}

// Parses the next expression by binding tokens until the left binding power is 
// <= right binding power (rbp)
Exp *ParseExpression(Parser *parser, int rbp) {
	Exp *left;
	Token *t = parser->tokens;
	ParserNext(parser);
	left = nud(parser, t);
	while (rbp < BindingPower(parser->tokens->type)) {
		t = parser->tokens;
		ParserNext(parser);
		left = led(parser, t, left);
	}
	return left;
}

File *ParseFile(Parser *parser) {
	Dcl **dcls = malloc(0);
	int dclCount = 0;
	while(parser->tokens->type != END) {
		Dcl *d = ParseDeclaration(parser);
		dcls = realloc(dcls, ++dclCount * sizeof(Dcl *));
		memcpy(dcls + dclCount - 1, &d, sizeof(Dcl *));
	}

	File *f = malloc(sizeof(File));
	f->dcls = dcls;
	f->dclCount = dclCount;

	return f;
}