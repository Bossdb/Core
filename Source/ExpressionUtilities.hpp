#pragma once
#include "Expression.hpp"
#include <arrow/array.h>
#include <cstdint>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <typeindex>
#include <typeinfo>
#include <utility>

namespace boss::utilities {
template <typename ExpressionSystem = DefaultExpressionSystem> class ExtensibleExpressionBuilder {
  Symbol const s;

public:
  explicit ExtensibleExpressionBuilder(Symbol const& s) : s(s){};
  explicit ExtensibleExpressionBuilder(const std::string& s) : s(Symbol(s)){};
  /**
   * This thing is a bit hacky: when construction Expression, some standard
   * libraries convert char const* to int or bool, not to std::string -- so I do
   * it explicitly
   */
  template <typename T>
  typename ExpressionSystem::Expression
  convertConstCharToStringAndOnToExpression(T const&& v) const {
    using Expression = typename ExpressionSystem::Expression;
    using ComplexExpression = typename ExpressionSystem::ComplexExpression;
    if constexpr(std::is_same_v<std::decay_t<decltype(v)>, char const*>) {
      return Expression(std::string((char const*)v));
    } else if constexpr(std::is_same_v<std::decay_t<decltype(v)>, ComplexExpression> ||
                        std::is_same_v<std::decay_t<decltype(v)>, Expression>) {
      return Expression(v.clone());
    } else {
      return Expression(v);
    }
  }
  template <typename T>
  std::enable_if_t<std::is_rvalue_reference_v<T&&>, typename ExpressionSystem::Expression>
  convertConstCharToStringAndOnToExpression(T&& v) const {
    return typename ExpressionSystem::Expression(std::forward<T>(v));
  }

  template <typename... Ts>
  typename ExpressionSystem::ComplexExpression operator()(Ts&&... args /*a*/) const {
    typename ExpressionSystem::ExpressionArguments argList;
    argList.reserve(sizeof...(Ts));
    (
        [this, &argList](auto&& arg) {
          argList.emplace_back(convertConstCharToStringAndOnToExpression<decltype(arg)>(
              std::forward<decltype(arg)>(arg)));
        }(std::move(args)),
        ...);
    return typename ExpressionSystem::ComplexExpression(s, std::move(argList));
  };

  friend typename ExpressionSystem::Expression
  operator|(typename ExpressionSystem::Expression const& expression,
            ExtensibleExpressionBuilder const& builder) {
    return builder(expression);
  };
  operator Symbol() const { return Symbol(s); } // NOLINT
};
using ExpressionBuilder = ExtensibleExpressionBuilder<>;
static ExpressionBuilder operator""_(const char* name, size_t /*unused*/) {
  return ExpressionBuilder(name);
};

namespace nasty {
// the ownership model is unclear -- we really need to fix that
static boss::ComplexExpression
arrowArrayToExpression(std::shared_ptr<arrow::Array> const& arrowPtr) {
  static_assert(sizeof(void*) == sizeof(std::int64_t),
                "pointers are not 64-bit -- this might break in funky ways");
  return "ArrowArrayPtr"_(reinterpret_cast<std::int64_t>(&arrowPtr));
}
static std::shared_ptr<arrow::Array> reconstructArrowArray(std::int64_t addressAsLong) {
  static_assert(sizeof(void*) == sizeof(std::int64_t),
                "pointers are not 64-bit -- this might break in funky ways");
  return *reinterpret_cast<std::shared_ptr<arrow::Array> const*>(addressAsLong);
}
} // namespace nasty

} // namespace boss::utilities

static std::ostream& operator<<(std::ostream& out, boss::Symbol const& thing) {
  return out << thing.getName();
}
template <typename... AdditionalCustomAtoms>
static std::ostream&
operator<<(std::ostream& out,
           boss::ComplexExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> const& e);
template <typename... AdditionalCustomAtoms>
static std::ostream&
operator<<(std::ostream& out,
           boss::ExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> const& thing) {
  std::visit(
      boss::utilities::overload([&](std::string const& value) { out << "\"" << value << "\""; },
                                [&](bool value) { out << (value ? "True" : "False"); },
                                [&](auto const& value) { out << value; }),
      thing);
  return out;
}
// a specialization for complex expressions is needed, or otherwise
// the complex expression and all its arguments has to be copied to be converted to an Expression
template <typename... AdditionalCustomAtoms>
static std::ostream&
operator<<(std::ostream& out,
           boss::ComplexExpressionWithAdditionalCustomAtoms<AdditionalCustomAtoms...> const& e) {
  out << e.getHead() << "[";
  if(!e.getArguments().empty()) {
    out << e.getArguments().front();
    for(auto it = next(e.getArguments().begin()); it != e.getArguments().end(); ++it) {
      out << "," << *it;
    }
  }
  out << "]";
  return out;
}

namespace boss {
class bad_variant_access : public std::bad_variant_access {
  std::string const whatString;

public:
  explicit bad_variant_access(std::string const& whatString) : whatString(whatString) {}
  const char* what() const noexcept override { return whatString.c_str(); }
};

template <typename T, typename TInput> auto&& get(TInput&& v) {
  try {
    return std::move(std::get<T>(std::forward<TInput>(v)));
  } catch(std::bad_variant_access const& e) {
    std::stringstream s;
    s << "expected and actual type mismatch in expression \"";
    if(!v.valueless_by_exception()) {
      s << v;
    } else {
      s << "valueless by exception";
    }
    static auto typenames = std::map<std::type_index, char const*>{{typeid(int64_t), "long"},
                                                                   {typeid(Symbol), "Symbol"},
                                                                   {typeid(bool), "bool"},
                                                                   {typeid(double_t), "double"},
                                                                   {typeid(std::string), "string"}};
    s << "\", expected "
      << (typenames.count(typeid(T)) ? typenames.at(typeid(T)) : typeid(T).name());
    throw bad_variant_access(s.str());
  }
};

} // namespace boss
