/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/sksl/ir/SkSLPrefixExpression.h"

#include "src/sksl/ir/SkSLBoolLiteral.h"
#include "src/sksl/ir/SkSLConstructor.h"
#include "src/sksl/ir/SkSLFloatLiteral.h"
#include "src/sksl/ir/SkSLIntLiteral.h"

namespace SkSL {

static std::unique_ptr<Expression> negate_operand(const Context& context,
                                                  std::unique_ptr<Expression> operand) {
    switch (operand->kind()) {
        case Expression::Kind::kFloatLiteral:
            // Convert -floatLiteral(1) to floatLiteral(-1).
            return std::make_unique<FloatLiteral>(operand->fOffset,
                                                  -operand->as<FloatLiteral>().value(),
                                                  &operand->type());

        case Expression::Kind::kIntLiteral:
            // Convert -intLiteral(1) to intLiteral(-1).
            return std::make_unique<IntLiteral>(operand->fOffset,
                                                -operand->as<IntLiteral>().value(),
                                                &operand->type());

        case Expression::Kind::kConstructor:
            // To be consistent with prior behavior, the conversion of a negated constructor into a
            // constructor of negative values is only performed when optimization is on.
            // Conceptually it's pretty similar to the int/float optimizations above, though.
            if (context.fConfig->fSettings.fOptimize && operand->isCompileTimeConstant()) {
                Constructor& ctor = operand->as<Constructor>();

                // We've found a negated constant constructor, e.g.:
                //     -float4(float3(floatLiteral(1)), floatLiteral(2))
                // To optimize this, the outer negation is removed and each argument is negated:
                //     float4(-float3(floatLiteral(1)), floatLiteral(-2))
                // Recursion will continue to push negation inwards as deeply as possible:
                //     float4(float3(floatLiteral(-1)), floatLiteral(-2))
                ExpressionArray args;
                args.reserve_back(ctor.arguments().size());
                for (std::unique_ptr<Expression>& arg : ctor.arguments()) {
                    args.push_back(negate_operand(context, std::move(arg)));
                }
                return Constructor::Make(context, ctor.fOffset, ctor.type(), std::move(args));
            }
            break;

        default:
            break;
    }

    // No simplified form; convert expression to Prefix(TK_MINUS, expression).
    return std::make_unique<PrefixExpression>(Token::Kind::TK_MINUS, std::move(operand));
}

static std::unique_ptr<Expression> logical_not_operand(const Context& context,
                                                       std::unique_ptr<Expression> operand) {
    if (operand->is<BoolLiteral>()) {
        // Convert !boolLiteral(true) to boolLiteral(false).
        const BoolLiteral& b = operand->as<BoolLiteral>();
        return std::make_unique<BoolLiteral>(operand->fOffset, !b.value(), &operand->type());
    }

    // No simplified form; convert expression to Prefix(TK_LOGICALNOT, expression).
    return std::make_unique<PrefixExpression>(Token::Kind::TK_LOGICALNOT, std::move(operand));
}

std::unique_ptr<Expression> PrefixExpression::Make(const Context& context, Operator op,
                                                   std::unique_ptr<Expression> base) {
    const Type& baseType = base->type();
    switch (op.kind()) {
        case Token::Kind::TK_PLUS:
            if (!baseType.componentType().isNumber()) {
                context.fErrors.error(base->fOffset,
                                      "'+' cannot operate on '" + baseType.displayName() + "'");
                return nullptr;
            }
            return base;

        case Token::Kind::TK_MINUS:
            if (!baseType.componentType().isNumber()) {
                context.fErrors.error(base->fOffset,
                                      "'-' cannot operate on '" + baseType.displayName() + "'");
                return nullptr;
            }
            return negate_operand(context, std::move(base));

        case Token::Kind::TK_PLUSPLUS:
            if (!baseType.isNumber()) {
                context.fErrors.error(base->fOffset,
                                      String("'") + op.operatorName() + "' cannot operate on '" +
                                      baseType.displayName() + "'");
                return nullptr;
            }
            if (!Analysis::MakeAssignmentExpr(base.get(), VariableReference::RefKind::kReadWrite,
                                              &context.fErrors)) {
                return nullptr;
            }
            break;

        case Token::Kind::TK_MINUSMINUS:
            if (!baseType.isNumber()) {
                context.fErrors.error(base->fOffset,
                                      String("'") + op.operatorName() + "' cannot operate on '" +
                                      baseType.displayName() + "'");
                return nullptr;
            }
            if (!Analysis::MakeAssignmentExpr(base.get(), VariableReference::RefKind::kReadWrite,
                                              &context.fErrors)) {
                return nullptr;
            }
            break;

        case Token::Kind::TK_LOGICALNOT:
            if (!baseType.isBoolean()) {
                context.fErrors.error(base->fOffset,
                                      String("'") + op.operatorName() + "' cannot operate on '" +
                                      baseType.displayName() + "'");
                return nullptr;
            }
            return logical_not_operand(context, std::move(base));

        case Token::Kind::TK_BITWISENOT:
            if (context.fConfig->strictES2Mode()) {
                // GLSL ES 1.00, Section 5.1
                context.fErrors.error(
                        base->fOffset,
                        String("operator '") + op.operatorName() + "' is not allowed");
                return nullptr;
            }
            if (!baseType.isInteger()) {
                context.fErrors.error(base->fOffset,
                                      String("'") + op.operatorName() + "' cannot operate on '" +
                                      baseType.displayName() + "'");
                return nullptr;
            }
            if (baseType.isLiteral()) {
                // The expression `~123` is no longer a literal; coerce to the actual type.
                base = baseType.scalarTypeForLiteral().coerceExpression(std::move(base), context);
            }
            break;

        default:
            SK_ABORT("unsupported prefix operator\n");
    }

    return std::make_unique<PrefixExpression>(op, std::move(base));
}

std::unique_ptr<Expression> PrefixExpression::constantPropagate(const IRGenerator& irGenerator,
                                                                const DefinitionMap& definitions) {
    if (this->operand()->isCompileTimeConstant()) {
        if (this->getOperator().kind() == Token::Kind::TK_MINUS) {
            // Constant-propagate negation onto compile-time constants.
            switch (this->operand()->kind()) {
                case Expression::Kind::kFloatLiteral:
                case Expression::Kind::kIntLiteral:
                case Expression::Kind::kConstructor:
                    return negate_operand(irGenerator.fContext, this->operand()->clone());

                default:
                    break;
            }
        } else if (this->getOperator().kind() == Token::Kind::TK_LOGICALNOT) {
            if (this->operand()->is<BoolLiteral>()) {
                return logical_not_operand(irGenerator.fContext, this->operand()->clone());
            }
        }
    }
    return nullptr;
}

}  // namespace SkSL
