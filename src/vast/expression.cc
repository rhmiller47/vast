#include "vast/expression.h"

#include <boost/variant/apply_visitor.hpp>
#include <ze/type/regex.h>
#include <ze/util/make_unique.h>
#include "vast/exception.h"
#include "vast/logger.h"
#include "vast/schema.h"
#include "vast/detail/ast/query.h"
#include "vast/detail/parser/parse.h"
#include "vast/detail/parser/query.h"

namespace vast {
namespace expr {

ze::value const& node::result() const
{
  return result_;
}

bool node::ready() const
{
  return ready_;
}

void node::reset()
{
  ready_ = false;
}

void extractor::feed(ze::event const* event)
{
  event_ = event;
  ready_ = false;
}

ze::event const* extractor::event() const
{
  return event_;
}

void timestamp_extractor::eval()
{
  result_ = event_->timestamp();
  ready_ = true;
}

void name_extractor::eval()
{
  result_ = event_->name();
  ready_ = true;
}

void id_extractor::eval()
{
  result_ = event_->id();
  ready_ = true;
}

offset_extractor::offset_extractor(std::vector<size_t> offsets)
  : offsets_(std::move(offsets))
{
}

void offset_extractor::eval()
{
  if (event_->empty())
  {
    result_ = ze::invalid;
  }
  else
  {
    ze::record const* record = event_;
    size_t i = 0;
    while (i < offsets_.size() - 1)
    {
      auto off = offsets_[i++];
      if (off >= record->size())
      {
        result_ = ze::invalid;
        break;
      }

      auto& value = (*record)[off];
      if (value.which() != ze::record_type)
      {
        result_ = ze::invalid;
        break;
      }

      record = &value.get<ze::record>();
    }

    auto last = offsets_[i];
    result_ = last < record->size() ? (*record)[last] : ze::invalid;
  }

  ready_ = true;
}

exists::exists(ze::value_type type)
  : type_(type)
{
}

void exists::feed(ze::event const* event)
{
  event_ = event;
  flat_size_ = event->flat_size();
  current_ = 0;
  ready_ = false;
}

void exists::reset()
{
  current_ = 0;
  ready_ = false;
}

void exists::eval()
{
  while (current_ < flat_size_)
  {
    auto& arg = event_->flat_at(current_++);
    if (type_ == arg.which())
    {
      result_ = arg;
      if (current_ == flat_size_)
        ready_ = true;

      return;
    }
  }

  ready_ = true;
}

void n_ary_operator::add(std::unique_ptr<node> operand)
{
  operands_.push_back(std::move(operand));
}

void n_ary_operator::reset()
{
  for (auto& op : operands_)
    op->reset();

  ready_ = false;
}

std::vector<std::unique_ptr<node>>& n_ary_operator::operands()
{
  return operands_;
}

std::vector<std::unique_ptr<node>> const& n_ary_operator::operands() const
{
  return operands_;
}

void conjunction::eval()
{
  ready_ = true;
  result_ = std::all_of(
      operands_.begin(),
      operands_.end(),
      [&](std::unique_ptr<node> const& operand) -> bool
      {
        if (! operand->ready())
          operand->eval();
        if (! operand->ready())
        ready_ = false;

        assert(operand->result().which() == ze::bool_type);
        return operand->result().get<bool>();
      });
}

void disjunction::eval()
{
  ready_ = true;
  result_ = std::any_of(
      operands_.begin(),
      operands_.end(),
      [&](std::unique_ptr<node> const& operand) -> bool
      {
        if (! operand->ready())
          operand->eval();
        if (! operand->ready())
        ready_ = false;

        assert(operand->result().which() == ze::bool_type);
        return operand->result().get<bool>();
      });

  if (result_.get<bool>() && ! ready_)
    ready_ = true;
}

relational_operator::relational_operator(relation_type type)
  : type_(type)
{
  switch (type_)
  {
    default:
      assert(! "invalid operator type");
      break;
    case match:
      op_ = [](ze::value const& lhs, ze::value const& rhs) -> bool
      {
        if (lhs.which() != ze::string_type || rhs.which() != ze::regex_type)
          return false;

        return rhs.get<ze::regex>().match(lhs.get<ze::string>());
      };
      break;
    case not_match:
      op_ = [](ze::value const& lhs, ze::value const& rhs) -> bool
      {
        if (lhs.which() != ze::string_type || rhs.which() != ze::regex_type)
          return false;

        return ! rhs.get<ze::regex>().match(lhs.get<ze::string>());
      };
      break;
    case in:
      op_ = [](ze::value const& lhs, ze::value const& rhs) -> bool
      {
        if (lhs.which() == ze::string_type &&
            rhs.which() == ze::regex_type)
          return rhs.get<ze::regex>().search(lhs.get<ze::string>());

        if (lhs.which() == ze::address_type &&
            rhs.which() == ze::prefix_type)
          return rhs.get<ze::prefix>().contains(lhs.get<ze::address>());

        return false;
      };
      break;
    case not_in:
      op_ = [](ze::value const& lhs, ze::value const& rhs) -> bool
      {
        if (lhs.which() == ze::string_type &&
            rhs.which() == ze::regex_type)
          return ! rhs.get<ze::regex>().search(lhs.get<ze::string>());

        if (lhs.which() == ze::address_type &&
            rhs.which() == ze::prefix_type)
          return ! rhs.get<ze::prefix>().contains(lhs.get<ze::address>());

        return false;
      };
      break;
    case equal:
      op_ = [](ze::value const& lhs, ze::value const& rhs)
      {
        return lhs == rhs;
      };
      break;
    case not_equal:
      op_ = [](ze::value const& lhs, ze::value const& rhs)
      {
        return lhs != rhs;
      };
      break;
    case less:
      op_ = [](ze::value const& lhs, ze::value const& rhs)
      {
        return lhs < rhs;
      };
      break;
    case less_equal:
      op_ = [](ze::value const& lhs, ze::value const& rhs)
      {
        return lhs <= rhs;
      };
      break;
    case greater:
      op_ = [](ze::value const& lhs, ze::value const& rhs)
      {
        return lhs > rhs;
      };
      break;
    case greater_equal:
      op_ = [](ze::value const& lhs, ze::value const& rhs)
      {
        return lhs >= rhs;
      };
      break;
  }
}

bool relational_operator::test(ze::value const& lhs, ze::value const& rhs) const
{
  return op_(lhs, rhs);
}

relation_type relational_operator::type() const
{
  return type_;
}

void relational_operator::eval()
{
  assert(operands_.size() == 2);

  auto& lhs = operands_[0];
  do
  {
    if (! lhs->ready())
      lhs->eval();

    auto& rhs = operands_[1];
    do
    {
      if (! rhs->ready())
        rhs->eval();

      ready_ = op_(lhs->result(), rhs->result());
      if (ready_)
        break;
    }
    while (! rhs->ready());

    if (ready_)
      break;
  }
  while (! lhs->ready());

  result_ = ready_;
  ready_ = true;
}

constant::constant(ze::value value)
{
  result_ = std::move(value);
  ready_ = true;
}

void constant::reset()
{
  // Do exactly nothing.
}

void constant::eval()
{
  // Do exactly nothing.
}

} // namespace expr

class expressionizer
{
public:
  typedef void result_type;

  expressionizer(expr::n_ary_operator* parent,
                 std::vector<expr::extractor*>& extractors)
    : parent_(parent)
    , extractors_(extractors)
  {
  }

  void operator()(detail::ast::query::clause const& operand)
  {
    boost::apply_visitor(*this, operand);
  }

  void operator()(detail::ast::query::tag_clause const& clause)
  {
    auto op = clause.op;
    if (invert_)
    {
      op = detail::ast::query::negate(op);
      invert_ = false;
    }
    auto relation = make_relational_operator(op);

    std::unique_ptr<expr::extractor> lhs;
    if (clause.lhs == "name")
      lhs = std::make_unique<expr::name_extractor>();
    else if (clause.lhs == "time")
      lhs = std::make_unique<expr::timestamp_extractor>();
    else if (clause.lhs == "id")
      lhs = std::make_unique<expr::id_extractor>();
    assert(lhs);
    extractors_.push_back(lhs.get());

    auto rhs = std::make_unique<expr::constant>(
        detail::ast::query::fold(clause.rhs));

    relation->add(std::move(lhs));
    relation->add(std::move(rhs));
    parent_->add(std::move(relation));
  }

  void operator()(detail::ast::query::type_clause const& clause)
  {
    auto op = clause.op;
    if (invert_)
    {
      op = detail::ast::query::negate(op);
      invert_ = false;
    }
    auto relation = make_relational_operator(op);

    auto lhs = std::make_unique<expr::exists>(clause.lhs);
    extractors_.push_back(lhs.get());

    auto rhs = std::make_unique<expr::constant>(
        detail::ast::query::fold(clause.rhs));

    relation->add(std::move(lhs));
    relation->add(std::move(rhs));
    parent_->add(std::move(relation));
  }

  void operator()(detail::ast::query::offset_clause const& clause)
  {
    auto op = clause.op;
    if (invert_)
    {
      op = detail::ast::query::negate(op);
      invert_ = false;
    }
    auto relation = make_relational_operator(op);

    auto lhs = std::make_unique<expr::offset_extractor>(clause.offsets);
    extractors_.push_back(lhs.get());

    auto rhs = std::make_unique<expr::constant>(
        detail::ast::query::fold(clause.rhs));

    relation->add(std::move(lhs));
    relation->add(std::move(rhs));
    parent_->add(std::move(relation));
  }

  void operator()(detail::ast::query::event_clause const& clause)
  {
    // The validation step of the query AST left the first element
    // untouched, as the name extractor uses it. Since all remaining
    // elements used to contain only a sequence of dereference operations
    // that yield a single offset, they are at this point condensed into
    // one element representing this offset.
    assert(clause.lhs.size() == 2);

    // TODO: Factor out this common optimization step of adding nodes directly
    // at the parent of they are conjunctions. We should create a visitor
    // interface and then an optimizer-visitor that performs such steps.
    expr::n_ary_operator* p;
    if (dynamic_cast<expr::conjunction*>(parent_))
    {
      p = parent_;
    }
    else
    {
      auto conj = std::make_unique<expr::conjunction>();
      p = conj.get();
      parent_->add(std::move(conj));
    }

    p->add(make_glob_node(clause.lhs[0]));

    auto op = clause.op;
    if (invert_)
    {
      op = detail::ast::query::negate(op);
      invert_ = false;
    }
    auto relation = make_relational_operator(op);

    // FIXME: use schema to determine correct offsets.
    std::vector<size_t> offsets{0};
    auto lhs = std::make_unique<expr::offset_extractor>(std::move(offsets));
    extractors_.push_back(lhs.get());
    relation->add(std::move(lhs));

    auto rhs = std::make_unique<expr::constant>(detail::ast::query::fold(clause.rhs));
    relation->add(std::move(rhs));

    p->add(std::move(relation));
  }

  void operator()(detail::ast::query::negated_clause const& clause)
  {
    invert_ = true;
    boost::apply_visitor(*this, clause.operand);
  }

private:
  std::unique_ptr<expr::node> make_glob_node(std::string const& expr)
  {
    // Determine whether we need a regular expression node or whether basic
    // equality comparison suffices. This check is relatively crude at the
    // moment: we just look whether the expression contains * or ?.
    auto glob = ze::regex("\\*|\\?").search(expr);

    auto op = make_relational_operator(
        glob ? detail::ast::query::match : detail::ast::query::equal);

    auto lhs = std::make_unique<expr::name_extractor>();
    extractors_.push_back(lhs.get());
    op->add(std::move(lhs));
    if (glob)
      op->add(std::make_unique<expr::constant>(ze::regex::glob(expr)));
    else
      op->add(std::make_unique<expr::constant>(expr));

    return std::move(op);
  }

  std::unique_ptr<expr::relational_operator>
  make_relational_operator(detail::ast::query::clause_operator op)
  {
    typedef expr::relation_type type;
    switch (op)
    {
      default:
        assert(! "missing relational operator in expression");
        return std::unique_ptr<expr::relational_operator>();
      case detail::ast::query::match:
        return std::make_unique<expr::relational_operator>(type::match);
      case detail::ast::query::not_match:
        return std::make_unique<expr::relational_operator>(type::not_match);
      case detail::ast::query::in:
        return std::make_unique<expr::relational_operator>(type::in);
      case detail::ast::query::not_in:
        return std::make_unique<expr::relational_operator>(type::not_in);
      case detail::ast::query::equal:
        return std::make_unique<expr::relational_operator>(type::equal);
      case detail::ast::query::not_equal:
        return std::make_unique<expr::relational_operator>(type::not_equal);
      case detail::ast::query::less:
        return std::make_unique<expr::relational_operator>(type::less);
      case detail::ast::query::less_equal:
        return std::make_unique<expr::relational_operator>(type::less_equal);
      case detail::ast::query::greater:
        return std::make_unique<expr::relational_operator>(type::greater);
      case detail::ast::query::greater_equal:
        return std::make_unique<expr::relational_operator>(type::greater_equal);
    }
  }

  expr::n_ary_operator* parent_;
  std::vector<expr::extractor*>& extractors_;
  bool invert_ = false;
};

expression::expression(expression const& other)
{
  parse(other.str_, other.schema_);
}

expression::expression(expression&& other)
  : str_(std::move(other.str_))
  , schema_(std::move(other.schema_))
  , root_(std::move(other.root_))
  , extractors_(std::move(other.extractors_))
{
}

expression& expression::operator=(expression other)
{
  using std::swap;
  swap(str_, other.str_);
  swap(schema_, other.schema_);
  swap(root_, other.root_);
  swap(extractors_, other.extractors_);
  return *this;
}

void expression::parse(std::string str, schema sch)
{
  if (str.empty())
      throw error::query("empty expression");

  str_ = std::move(str);
  schema_ = std::move(sch);
  extractors_.clear();

  detail::ast::query::query ast;
  if (! detail::parser::parse<detail::parser::query>(str_, ast))
    throw error::query("syntax error", str_);

  if (! detail::ast::query::validate(ast))
    throw error::query("semantic error", str_);

  if (ast.rest.empty())
  {
    /// WLOG, we can always add a conjunction as root.
    auto conjunction = std::make_unique<expr::conjunction>();
    expressionizer visitor(conjunction.get(), extractors_);
    boost::apply_visitor(std::ref(visitor), ast.first);
    root_ = std::move(conjunction);
  }
  else
  {
    // First, split the query expression at each OR node.
    std::vector<detail::ast::query::query> ors{detail::ast::query::query{ast.first}};
    for (auto& clause : ast.rest)
      if (clause.op == detail::ast::query::logical_or)
        ors.emplace_back(detail::ast::query::query{clause.operand});
      else
        ors.back().rest.push_back(clause);

    // Then create a conjunction for each set of subsequent AND nodes between
    // two OR nodes.
    auto disjunction = std::make_unique<expr::disjunction>();
    for (auto& ands : ors)
      if (ands.rest.empty())
      {
        expressionizer visitor(disjunction.get(), extractors_);
        boost::apply_visitor(std::ref(visitor), ands.first);
      }
      else
      {
        auto conjunction = std::make_unique<expr::conjunction>();
        expressionizer visitor(conjunction.get(), extractors_);
        boost::apply_visitor(std::ref(visitor), ands.first);
        for (auto clause : ands.rest)
        {
          assert(clause.op == detail::ast::query::logical_and);
          boost::apply_visitor(std::ref(visitor), clause.operand);
        }

        disjunction->add(std::move(conjunction));
      }

    root_ = std::move(disjunction);
  }

  assert(! extractors_.empty());
}

bool expression::eval(ze::event const& event)
{
  for (auto ext : extractors_)
    ext->feed(&event);

  while (! root_->ready())
    root_->eval();

  auto& r = root_->result();

  assert(r.which() == ze::bool_type);

  root_->reset();
  return r.get<bool>();
}

void expression::accept(expr::const_visitor& v) const
{
  assert(root_);
  root_->accept(v);
}

void expression::accept(expr::visitor& v)
{
  assert(root_);
  root_->accept(v);
}

bool operator==(expression const& x, expression const& y)
{
  return x.str_ == y.str_ && x.schema_ == y.schema_;
}

} // namespace vast
