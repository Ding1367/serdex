// The MIT License (MIT)
//
// Copyright (c) 2026 Ding1367
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef SERDEX_LIBRARY_H
#define SERDEX_LIBRARY_H
#include <cassert>
#include <unordered_map>
#include <vector>
#include <variant>
#include <functional>

namespace serdex {
    struct program;
    struct value;

    template <typename K, typename C>
    struct collection;

    struct object;
    struct array;
    struct out_of_order_array;

    using index_type = std::variant<std::string, size_t>;
    using value_type = std::variant<std::string, int64_t, double, bool, program, nullptr_t, object, array, out_of_order_array>;

    using value_subset = std::variant<nullptr_t, int64_t, std::string, bool, double>;

    namespace internal {
        template <typename T>
        struct pair_erasure {
            static const T &value(const T &t) {
                return t;
            }
        };

        template <typename K, typename V>
        struct pair_erasure<std::pair<K, V>> {
            static const V &value(const std::pair<K, V> &v_pair) {
                return v_pair.second;
            }
        };

        template <typename T>
        struct is_collection : std::false_type {};

        template <typename K, typename C>
        struct is_collection<collection<K, C>> : std::true_type {
            using key_type = K;
            using stl_type = C;
        };
    }

    template <typename K, typename C>
    struct collection {
        C values;

        [[nodiscard]] bool operator==(const collection &other) const {
            return values == other.values;
        }
        [[nodiscard]] bool operator==(const C &other) const {
            return values == other;
        }

        [[nodiscard]] size_t size() const {
            return values.size();
        }

        [[nodiscard]] value &at(K i) {
            return values.at(i);
        }
        [[nodiscard]] const value &at(K i) const {
            return values.at(i);
        }
        value &emplace(K i, const value &v) {
            return values[i] = v;
        }
        [[nodiscard]] bool contains(const value &value) const {
            return std::ranges::any_of(values, [&value]<typename Z>(const Z &element) -> bool {
                const auto &other = internal::pair_erasure<Z>::value(element);
                return other == value;
            });
        }
        [[nodiscard]] bool contains(K key) const {
            if constexpr (std::is_same_v<C, std::vector<value>>) {
                return key < values.size();
            }
            return values.contains(key);
        }
    };

    using object_map_type = std::unordered_map<std::string, value>;
    struct object : collection<std::string, object_map_type> {};
    struct array : collection<size_t, std::vector<value>> {
        value &emplace_back(const value &v);
    };
    struct out_of_order_array : collection<size_t, std::unordered_map<size_t, value>> {
        bool can_be_array = true;
        size_t max_index = 0;

        value &emplace(size_t i, const value &v);
    };

    enum class opcode {
        field, store_variable, push_variable, push_value, op_and, op_not, op_eq, op_add, op_sub, op_if
    };

    struct instruction;

    struct program {
        std::vector<instruction> prog = {};

        void push_ins(const instruction &ins);
        [[nodiscard]] size_t size() const;
    };

    struct value {
        value_type val;

        value();

        template <typename T>
        value(T t) : val(std::move(t)){}

        value(value_type val);

        operator value_type() const;

        template <typename T>
        T &get() {
            return std::get<T>(val);
        }

        template <typename T>
        const T &get() const {
            return std::get<T>(val);
        }

        template <typename T>
        [[nodiscard]] bool holds() const {
            return std::holds_alternative<T>(val);
        }

        [[nodiscard]] value &at(const std::string &key) {
            return get<object>().at(key);
        }
        [[nodiscard]] const value &at(const std::string &key) const {
            return get<object>().at(key);
        }
        [[nodiscard]] value &at(size_t key) {
            if (holds<out_of_order_array>()) {
                return get<out_of_order_array>().at(key);
            }
            return get<array>().at(key);
        }
        [[nodiscard]] const value &at(size_t key) const {
            if (holds<out_of_order_array>()) {
                return get<out_of_order_array>().at(key);
            }
            return get<array>().at(key);
        }
        value &emplace(const value &v) {
            return get<array>().emplace_back(v);
        }

        [[nodiscard]] size_t size() {
            return std::visit([](const auto &value) {
                if constexpr (requires { value.size(); }) {
                    return value.size();
                } else {
                    return (size_t)1;
                }
            }, val);
        }

        value &emplace(size_t key, const value &v) {
            return get<out_of_order_array>().emplace(key, v);
        }

        value &emplace(const std::string &key, const value &v) {
            return get<object>().emplace(key, v);
        }

        [[nodiscard]] bool contains(const std::string &key) const {
            if (!holds<object>())
                return false;
            return get<object>().contains(key);
        }

        [[nodiscard]] bool contains(size_t key) const {
            return std::visit([key]<typename T>(const T &value){
                if constexpr (internal::is_collection<T>::value) {
                    if constexpr (std::is_same_v<size_t, typename internal::is_collection<T>::key_type>) {
                        return value.contains(key);
                    } else {
                        return value.contains(std::to_string(key));
                    }
                }
                return false;
            }, val);
        }

        value &operator[](const std::string &key) {
            return at(key);
        }

        const value &operator[](const std::string &key) const {
            return at(key);
        }

        template <typename T>
        bool operator==(const T &other) const {
            return std::visit([&other](const auto &value) {
                if constexpr (requires { other == value; }) {
                    return other == value;
                }
                return false;
            }, val);
        }

        template <typename T>
        bool operator!=(const T &other) const {
            return !(other == *this);
        }

        bool operator==(const value &other) const;

        bool operator!=(const value &other) const {
            return !(other == *this);
        }

        value &operator[](size_t key) {
            return at(key);
        }

        const value &operator[](size_t key) const {
            return at(key);
        }
    };

    struct instruction {
        opcode op;
        value value;
    };

    struct evaluator {
        std::unordered_map<std::string, value_subset> variables = {};
        std::vector<value_subset> stack = {};
        const program *prog = nullptr;
        value_subset feedback;
        size_t pc = 0;

        void reset() {
            variables.clear();
            stack.clear();
            pc = 0;
        }

        void init(const program *prog_) {
            assert(pc == 0);
            prog = prog_;
        }

        [[nodiscard]] bool done() const {
            return pc >= prog->prog.size();
        }

        std::optional<value> next();

    private:
        value_subset pop() {
            value_subset sub = stack.at(stack.size() - 1);
            stack.pop_back();
            return sub;
        }
    };

    namespace internal {
        struct scope {
            value_type value;
            scope *parent;
            std::optional<index_type> index;
            std::vector<size_t> if_blocks = {};
            std::vector<std::string> variable_names = {};
            int t;
            size_t implied_index = 0;
            bool needs_comma = false;

            explicit scope(value_type v) : value(std::move(v)), parent(nullptr) {
                t = v.index();
            }

            size_t pop_if() {
                size_t n = if_blocks.at(if_blocks.size() - 1);
                if_blocks.pop_back();
                return n;
            }

            void push_if() {
                auto &prog = std::get<program>(value);
                if_blocks.push_back(prog.size());
                prog.push_ins(instruction {
                    .op = opcode::op_if,
                    .value = nullptr
                });
            }
        };
    }

    struct parser {
        std::optional<value> parse(std::istream &stream);
    private:
        int line = 1, col = 1;
        internal::scope *m_scope = {};
        void push_scope(value_type val);
        value_type pop_scope();
        void skip_ws(std::istream &s);
        int get(std::istream &s);
    };
}

std::ostream &operator<<(std::ostream &, const serdex::value &);

#endif // SERDEX_LIBRARY_H