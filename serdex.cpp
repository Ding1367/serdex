#include "serdex.h"
#include <iostream>
using namespace serdex;

namespace serdex::internal {
    template <typename ...T>
    void parse_num(const std::string &token, std::variant<T...> &val) {
        size_t p = 0;
        bool sign = false;
        uint64_t i = 0;
        int n_frac_digits = 0;
        uint64_t denom = 0;
        bool is_float = false;
        size_t start_of_numeric = -1;
        while (p < token.size()) {
            char c = token[p++];
            if (c == '.') {
                assert(!is_float);
                is_float = true;
                continue;
            }
            if (is_float) {
                n_frac_digits++;
                if (denom == 0) {
                    denom = 10;
                } else {
                    denom = denom * 10;
                }
            }
            if (c == '-') {
                sign = !sign;
            } else if (isspace(c) || c == '+') {
                continue;
            } else if (isdigit(c)) {
                if (start_of_numeric == -1) {
                    start_of_numeric = p - 1;
                }
                i = i * 10 + c - '0';
            }
        }
        if (!is_float) {
            val = sign ? -(int64_t)i : (int64_t)i;
            return;
        }
        double d = std::stod(token.substr(start_of_numeric));
        val = sign ? -d : d;
    }

    void eval_expr(const std::vector<std::string>& tokens, scope* scope) {
        int bin_op_state = 0;
        std::string op;
        auto &prog = std::get<program>(scope->value);
        for (const auto &tok : tokens){
            if (bin_op_state != 1) {
                // TODO: handle multi token immediates (- 5) or unary operations (- index, when op_neg is added)
                if (!isdigit(tok[0])) {
                    // TODO: use scope->variable_names
                    prog.push_ins(instruction {
                        .op = opcode::push_variable,
                        .value = tok
                    });
                } else {
                    std::variant<int64_t, double> result;
                    parse_num(tok, result);
                    prog.push_ins(instruction {
                        .op = opcode::push_value,
                        .value = std::get<0>(result)
                    });
                }
                bin_op_state++;
                if (bin_op_state == 3) {
                    if (op == "==") {
                        prog.push_ins(instruction {
                            .op = opcode::op_eq
                        });
                    } else if (op == "&") {
                        prog.push_ins(instruction {
                            .op = opcode::op_and
                        });
                    }
                    bin_op_state = 0;
                }
            } else {
                op = tok;
                bin_op_state++;
            }
        }
    }
}

value& array::emplace_back(const value& v) {
    return values.emplace_back(v);
}

value& out_of_order_array::emplace(size_t i, const value& v) {
    if (i > max_index) {
        max_index = i;
        can_be_array = values.size() == max_index;
    }
    return values[i] = v;
}

void program::push_ins(const instruction& ins) {
    prog.push_back(ins);
}

size_t program::size() const {
    return prog.size();
}

value::value() : val(nullptr) {
}

value::value(value_type val) : val(std::move(val)) {
}

value::operator value_type() const {
    return val;
}

bool value::operator==(const value& other) const {
    if (other.val.index() != val.index())
        return false;
    return std::visit([&other]<typename T>(const T &value) {
        if constexpr (requires { T {} == value; }) {
            return value == other.get<T>();
        }
        return false;
    }, val);
}

std::optional<value> evaluator::next() {
    const auto &ins_list = prog->prog;
    while (true) {
        if (pc >= ins_list.size()) {
            return {};
        }
        const auto &ins = ins_list[pc];
        pc++;
        switch (ins.op) {
        case opcode::store_variable:
            variables[ins.value.get<std::string>()] = feedback;
            continue;
        case opcode::push_variable:
            stack.push_back(variables.at(ins.value.get<std::string>()));
            continue;
        case opcode::push_value:
            if (!std::visit([this](const auto &value) {
                if constexpr (requires {
                    value_subset { value };
                }) {
                    stack.emplace_back(value);
                    return true;
                }
                return false;
            }, (value_type)ins.value)) {
                throw std::logic_error("attempted to push incompatible value onto stack");
            }
            continue;
        case opcode::field:
            return ins.value;
        case opcode::op_if: {
            auto cond = pop();
            if (std::holds_alternative<bool>(cond) && !std::get<bool>(cond)) {
                pc = static_cast<size_t>(ins.value.get<int64_t>());
            }
            continue;
        }
        case opcode::op_eq: {
            auto b = pop();
            auto a = pop();
            stack.emplace_back(a == b);
            continue;
        }
        case opcode::op_not: {
            auto x = pop();
            if (std::holds_alternative<bool>(x)) {
                stack.emplace_back(!std::get<bool>(x));
            } else if (std::holds_alternative<int64_t>(x)) {
                stack.emplace_back(~std::get<int64_t>(x));
            } else {
                stack.emplace_back(0);
            }
            continue;
        }
        case opcode::op_and: {
            auto b = pop();
            auto a = pop();
            if (a.index() != b.index() || a.index() != 1 || a.index() != 3){
                stack.emplace_back(0);
            } else {
                if (a.index() == 1) {
                    stack.emplace_back(std::get<bool>(a) && std::get<bool>(b));
                } else {
                    stack.emplace_back(std::get<int64_t>(a) & std::get<int64_t>(b));
                }
            }
            continue;
        }
        default: assert(0);
        }
    }
}

std::optional<value> parser::parse(std::istream& stream) {
    line = col = 1;
    bool program_mode = false;
    bool comma = false;
    std::optional<value> value = {};
    value_type cur;
    int collection_parse_stage = 0;
    while (true) {
        skip_ws(stream);
        int c = get(stream);
        if (c == '/') {
            assert(get(stream) == '/');
            while (c = stream.peek(), c != EOF && c != '\n') {
                get(stream);
            }
            continue;
        }
        if (c == '@') {
            program_mode = true;
            continue;
        }
        if (c == '[') {
            if (program_mode) {
                push_scope(program {});
            } else {
                push_scope(out_of_order_array {});
            }
            collection_parse_stage = 0;
            comma = false;
            continue;
        }
        if (c == '{') {
            push_scope(object {});
            collection_parse_stage = 0;
            comma = false;
            continue;
        }
        if (c == ']' || c == '}') {
            assert(m_scope);
            if (c == ']')
                assert(m_scope->t != 6);
            else
                assert(m_scope->t == 6);
            cur = pop_scope();
            comma = false;
        } else if (m_scope && m_scope->needs_comma && collection_parse_stage == 0) {
            if (comma) {
                assert(c != ',');
                comma = false;
            } else {
                assert(c == ',');
                comma = true;
                continue;
            }
        }
        if (collection_parse_stage == 1) {
            assert(c == ':');
            collection_parse_stage++;
            continue;
        }
        program_mode = false;
        if (isalpha(c) || c == '_' || c == '$') {
            std::string k = {};
            k.push_back(c);
            while (true) {
                c = stream.peek();
                if (!isalnum(c) && c != '_' && c != '$') {
                    break;
                }
                k.push_back(get(stream));
            }
            // is it boolean or null?
            if (collection_parse_stage != 0) {
                if (k == "null") {
                    cur = nullptr;
                } else if (k == "true") {
                    cur = true;
                } else if (k == "false") {
                    cur = false;
                }
            } else {
                assert(m_scope && (m_scope->t == 6 || m_scope->t == 4));
                if (m_scope->t == 4) {
                    auto &prog = std::get<program>(m_scope->value);
                    if (k == "if") {
                        skip_ws(stream);
                        // try to parse a boolean expression
                        std::vector<std::string> tokens = {};
                        std::string expr = {};
                        while (c = stream.peek(), c != ',') {
                            get(stream);
                            if (isspace(c)) {
                                if (expr.empty()) continue;
                                tokens.push_back(expr);
                                expr.clear();
                            } else {
                                expr.push_back(c);
                            }
                        }
                        if (!expr.empty())
                            tokens.push_back(expr);
                        internal::eval_expr(tokens, m_scope);
                        m_scope->push_if();
                        continue;
                    }
                    if (k == "endif") {
                        size_t p = m_scope->pop_if();
                        prog.prog.at(p).value = (int64_t)prog.prog.size();
                        continue;
                    }
                }
                m_scope->index = std::move(k);
                collection_parse_stage++;
                continue;
            }
        }
        if (isdigit(c) || c == '+' || c == '-' || c == '.') {
            // parse number
            std::string n = {};
            n.push_back(c);
            bool first_digit = isdigit(c);
            while (true) {
                c = stream.peek();
                if (isspace(c) && !first_digit) {
                    skip_ws(stream);
                    c = stream.peek();
                }
                if (!isdigit(c) && c != '-' && c != '+' && c != '.') {
                    break;
                }
                first_digit = first_digit || isdigit(c);
                n.push_back(get(stream));
            }
            skip_ws(stream);
            internal::parse_num(n, cur);
            if (stream.peek() == ':') {
                assert(collection_parse_stage == 0);
                assert(m_scope);
                get(stream);
                size_t k = std::get<int64_t>(cur);
                if (m_scope->t == 6) {
                    m_scope->index = std::to_string(k);
                } else {
                    m_scope->index = k;
                }
                collection_parse_stage = 2;
                continue;
            }
        }
        if (c == '"') {
            std::string s = {};
            while (true) {
                c = get(stream);
                if (c == '"') {
                    break;
                }
                if (c == '\\') {
                    int escape = get(stream);
                    if (escape == 'n') {
                        c = '\n';
                    } else if (escape == 't') {
                        c = '\t';
                    } else if (escape == 'b') {
                        c = '\b';
                    } else {
                        c = escape;
                    }
                }
                s.push_back(c);
            }
            cur = s;
        }
        if (!m_scope) {
            value = cur;
            break;
        }
        collection_parse_stage = 0;
        if (m_scope->t == 6) {
            assert(m_scope->index.has_value());
            auto index_val = m_scope->index.value();
            std::string k;
            if (!std::holds_alternative<std::string>(index_val)) {
                k = std::to_string(std::get<size_t>(index_val));
            } else {
                k = std::get<std::string>(index_val);
            }
            auto &obj = std::get<object>(m_scope->value);
            obj.emplace(k, std::move(cur));
            // used for commas
            m_scope->implied_index++;
        } else if (m_scope->t == 8) {
            auto &ooo_array = std::get<out_of_order_array>(m_scope->value);
            size_t key;
            if (m_scope->index) {
                auto k = m_scope->index.value();
                assert(std::holds_alternative<size_t>(k));
                key = std::get<size_t>(k);
                if (key == m_scope->implied_index) {
                    m_scope->implied_index++;
                }
            } else {
                key = m_scope->implied_index++;
            }
            ooo_array.emplace(key, std::move(cur));
        } else if (m_scope->t == 4) {
            auto &prog = std::get<program>(m_scope->value);
            prog.push_ins(instruction {
                .op = opcode::field,
                .value = std::move(cur)
            });
            if (m_scope->index.has_value()) {
                auto s = std::get<std::string>(m_scope->index.value());
                prog.push_ins(instruction {
                    .op = opcode::store_variable,
                    .value = s
                });
                m_scope->variable_names.push_back(s);
            }
        }
        m_scope->needs_comma = true;
        m_scope->index = {};
    }
    skip_ws(stream);
    if (!m_scope) {
        int c = get(stream);
        if (c != EOF) {
            value = {};
        }
    }
    return value;
}

void parser::push_scope(value_type val) {
    auto *s = new internal::scope(std::move(val));
    s->parent = m_scope;
    m_scope = s;
}

value_type parser::pop_scope() {
    auto *s = m_scope;
    value_type v = std::move(s->value);
    m_scope = s->parent;
    if (s->t == 8) {
        // try to convert to array
        auto &ooo_array = std::get<out_of_order_array>(v);
        array a = {};
        a.values.resize(ooo_array.size());

        if (ooo_array.can_be_array) {
            for (const auto &[index, value] : ooo_array.values)
                a.emplace(index, value);
            v = a;
        }
    }
    delete s;
    return v;
}

void parser::skip_ws(std::istream& s) {
    while (isspace(s.peek())) {
        get(s);
    }
}

int parser::get(std::istream& s) {
    int c = s.get();
    if (c == '\n') {
        line++;
        col = 0;
    }
    if (c != EOF)
        col++;
    return c;
}

template <bool escape_strings = false>
static std::ostream& print(std::ostream &os, const value &v) {
    std::visit([&os]<typename T>(const T &val) {
        if constexpr (std::is_same_v<T, array>) {
            os << '[';
            for (const auto &e : val.values) {
                print<true>(os, e);
                os << ',';
            }
            os << ']';
        } else if constexpr (std::is_same_v<T, out_of_order_array>) {
            os << '[';
            for (const auto &[i, e] : val.values) {
                os << i << ':';
                print<true>(os, e);
                os << ',';
            }
            os << ']';
        } else if constexpr (std::is_same_v<T, object>) {
            os << '{';
            for (const auto &[i, e] : val.values) {
                os << i << ':';
                print<true>(os, e);
                os << ',';
            }
            os << '}';
        } else if constexpr (std::is_same_v<T, program>) {
            os << "<program>";
        } else if constexpr (std::is_same_v<T, std::string> && escape_strings) {
            os << '"';
            for (const auto c : val) {
                if (c == '\n') {
                    os << "\\n";
                } else if (c == '\t') {
                    os << "\\t";
                } else if (c == '\b') {
                    os << "\\b";
                } else if (c == '\\') {
                    os << "\\";
                } else {
                    os << c;
                }
            }
            os << '"';
        } else {
            os << val;
        }
    }, (value_type)v);
    return os;
}

std::ostream& operator<<(std::ostream &os, const value &v) {
    return print(os, v);
}
