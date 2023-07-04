#include "taichi/ir/ir.h"
#include "taichi/ir/frontend_ir.h"
#include "taichi/ir/statements.h"
#include "taichi/ir/expression_printer.h"

namespace taichi::lang {

class FrontendTypeCheck : public IRVisitor {
  void check_cond_type(const Stmt *stmt) {
    Expr cond;
    std::string stmt_name;
    if (stmt->is<FrontendAssertStmt>()) {
      stmt_name = "assert";
      cond = stmt->as<FrontendAssertStmt>()->cond;
    } else if (stmt->is<FrontendIfStmt>()) {
      stmt_name = "if";
      cond = stmt->as<FrontendIfStmt>()->condition;
    } else if (stmt->is<FrontendWhileStmt>()) {
      stmt_name = "while";
      cond = stmt->as<FrontendWhileStmt>()->cond;
    } else {
      TI_STOP;
    }

    DataType cond_type = cond.get_rvalue_type();
    if (!cond_type->is<PrimitiveType>() || !is_integral(cond_type)) {
      ErrorEmitter(
          TaichiTypeError(), stmt,
          fmt::format("`{0}` conditions must be an integer; found {1}. "
                      "Consider using "
                      "`{0} x != 0` instead of `{0} x` for float values.",
                      stmt_name, cond_type->to_string()));
    }
  }

 public:
  explicit FrontendTypeCheck() {
    allow_undefined_visitor = true;
  }

  void visit(Block *block) override {
    std::vector<Stmt *> stmts;
    // Make a copy since type casts may be inserted for type promotion.
    for (auto &stmt : block->statements)
      stmts.push_back(stmt.get());
    for (auto stmt : stmts)
      stmt->accept(this);
  }

  void visit(FrontendExternalFuncStmt *stmt) override {
    // TODO: noop for now; add typechecking after we have type specification
  }

  void visit(FrontendExprStmt *stmt) override {
    // Noop
  }

  void visit(FrontendAllocaStmt *stmt) override {
    // Noop
  }

  void visit(FrontendSNodeOpStmt *stmt) override {
    // Noop
  }

  void visit(FrontendAssertStmt *stmt) override {
    check_cond_type(stmt);
  }

  void visit(FrontendAssignStmt *stmt) override {
    auto lhs_type = stmt->lhs->ret_type.ptr_removed();
    auto rhs_type = stmt->rhs->ret_type.ptr_removed();

    // No implicit cast at frontend for now
    if (is_tensor(lhs_type) && is_tensor(rhs_type) &&
        lhs_type.get_shape() != rhs_type.get_shape()) {
      ErrorEmitter(TaichiTypeError(), stmt,
                   fmt::format("cannot assign '{}' to '{}'",
                               rhs_type->to_string(), lhs_type->to_string()));
    }

    if (lhs_type != rhs_type) {
      auto promoted = promoted_type(lhs_type, rhs_type);
      if (lhs_type != promoted) {
        ErrorEmitter(TaichiCastWarning(), stmt,
                     fmt::format("Assign may lose precision: {} <- {}",
                                 lhs_type->to_string(), rhs_type->to_string()));
      }
    }
  }

  void visit(FrontendIfStmt *stmt) override {
    // TODO: use PrimitiveType::u1 when it's supported
    check_cond_type(stmt);
    if (stmt->true_statements)
      stmt->true_statements->accept(this);
    if (stmt->false_statements)
      stmt->false_statements->accept(this);
  }

  void visit(FrontendPrintStmt *stmt) override {
    TI_ASSERT(stmt->contents.size() == stmt->formats.size());
    for (int i = 0; i < stmt->contents.size(); i++) {
      auto const &content = stmt->contents[i];
      auto const &format = stmt->formats[i];
      if (std::holds_alternative<std::string>(content) || !format.has_value()) {
        continue;
      }

      Expr const &expr = std::get<Expr>(content);
      TI_ASSERT(expr.expr != nullptr);
      DataType data_type = expr.get_rvalue_type();
      if (data_type->is<TensorType>()) {
        data_type = DataType(data_type->as<TensorType>()->get_element_type());
      }
      TI_ASSERT(!format->empty());
      std::string const &format_spec = format.value();
      auto const &conversion = format_spec.back();

      // all possible conversions in printf
      constexpr std::string_view conversions = "csdioxXufFeEaAgGnp";
      if (conversions.find(conversion) == std::string::npos) {
        // allow empty conversion
        continue;
      }

      // convensions categorized by data type.
      constexpr std::string_view unsupported_group = "csnp";
      constexpr std::string_view signed_group = "di";
      constexpr std::string_view unsigned_group = "oxXu";
      constexpr std::string_view real_group = "fFeEaAgG";

      if (unsupported_group.find(conversion) != std::string::npos) {
        ErrorEmitter(
            TaichiTypeError(), stmt,
            fmt::format("conversion '{}' is not supported.", conversion));
      }

      if ((real_group.find(conversion) != std::string::npos &&
           !is_real(data_type)) ||
          (signed_group.find(conversion) != std::string::npos &&
           !(is_integral(data_type) && is_signed(data_type))) ||
          (unsigned_group.find(conversion) != std::string::npos &&
           !(is_integral(data_type) && is_unsigned(data_type)))) {
        ErrorEmitter(TaichiTypeError(), stmt,
                     fmt::format("'{}' doesn't match '{}'.", format_spec,
                                 data_type->to_string()));
      }
    }
  }

  void visit(FrontendForStmt *stmt) override {
    stmt->body->accept(this);
  }

  void visit(FrontendFuncDefStmt *stmt) override {
    stmt->body->accept(this);
    // Determine ret_type after this is actually used
  }

  void visit(FrontendBreakStmt *stmt) override {
    // Noop
  }

  void visit(FrontendContinueStmt *stmt) override {
    // Noop
  }

  void visit(FrontendWhileStmt *stmt) override {
    check_cond_type(stmt);
    stmt->body->accept(this);
  }

  void visit(FrontendReturnStmt *stmt) override {
    // Noop
  }
};

namespace irpass {

void frontend_type_check(IRNode *root) {
  TI_AUTO_PROF;
  FrontendTypeCheck checker;
  root->accept(&checker);
}

}  // namespace irpass

}  // namespace taichi::lang
