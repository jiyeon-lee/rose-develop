#ifndef REVERSE_COMPUTATION_NORMALIZATION
#define REVERSE_COMPUTATION_NORMALIZATION

#include <rose.h>

// Normalize an event function.
void normalizeEvent(SgFunctionDefinition* func_def);

// Transform a modifying expression into several ones contained in a comma 
// operator expression. The transformations are:
//
//    a = b  ==>  a = b, a
//    --a  ==>  --a, a
//    a--  ==>  t = a, --a, t
//    a && b  ==>  t = a, t && t = b, t
//    a || b  ==>  t = a, t || t = b, t
//
void getAndReplaceModifyingExpression(SgExpression*& exp);

// Split a comma expression into several statements.
void splitCommaOpExp(SgExpression* exp);

// Propagate comma operation expressions, e.g. (a, b) + c ==> a, (b + c).
SgExpression* propagateCommaOpExp(SgExpression* exp);

// Propagate conditional operation expressions, 
// e.g. (a ? b : c) = d ==> a ? (b = d) : (c = d).
SgExpression* propagateConditionalExp(SgExpression* exp);

inline SgExpression* propagateCommaOpAndConditionalExp(SgExpression* exp)
{
    exp = propagateCommaOpExp(exp);
    return propagateConditionalExp(exp);
}

#endif
