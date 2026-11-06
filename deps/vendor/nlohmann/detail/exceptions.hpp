#pragma once

#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/detail/value_t.hpp>
#include <nlohmann/detail/string_escape.hpp>
#include <nlohmann/detail/input/position_t.hpp>
#include <nlohmann/detail/macro_scope.hpp>

namespace nlohmann
{
namespace detail
{
































class exception : public std::exception
{
  public:

    const char* what() const noexcept override
    {
        return m.what();
    }


    const int id; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)

  protected:
    JSON_HEDLEY_NON_NULL(3)
    exception(int id_, const char* what_arg) : id(id_), m(what_arg) {}

    static std::string name(const std::string& ename, int id_)
    {
        return "[json.exception." + ename + "." + std::to_string(id_) + "] ";
    }

    template<typename BasicJsonType>
    static std::string diagnostics(const BasicJsonType& leaf_element)
    {
#if JSON_DIAGNOSTICS
        std::vector<std::string> tokens;
        for (const auto* current = &leaf_element; current->m_parent != nullptr; current = current->m_parent)
        {
            switch (current->m_parent->type())
            {
                case value_t::array:
                {
                    for (std::size_t i = 0; i < current->m_parent->m_value.array->size(); ++i)
                    {
                        if (&current->m_parent->m_value.array->operator[](i) == current)
                        {
                            tokens.emplace_back(std::to_string(i));
                            break;
                        }
                    }
                    break;
                }

                case value_t::object:
                {
                    for (const auto& element : *current->m_parent->m_value.object)
                    {
                        if (&element.second == current)
                        {
                            tokens.emplace_back(element.first.c_str());
                            break;
                        }
                    }
                    break;
                }

                case value_t::null: // LCOV_EXCL_LINE
                case value_t::string: // LCOV_EXCL_LINE
                case value_t::boolean: // LCOV_EXCL_LINE
                case value_t::number_integer: // LCOV_EXCL_LINE
                case value_t::number_unsigned: // LCOV_EXCL_LINE
                case value_t::number_float: // LCOV_EXCL_LINE
                case value_t::binary: // LCOV_EXCL_LINE
                case value_t::discarded: // LCOV_EXCL_LINE
                default:   // LCOV_EXCL_LINE
                    break; // LCOV_EXCL_LINE
            }
        }

        if (tokens.empty())
        {
            return "";
        }

        return "(" + std::accumulate(tokens.rbegin(), tokens.rend(), std::string{},
                                     [](const std::string & a, const std::string & b)
        {
            return a + "/" + detail::escape(b);
        }) + ") ";
#else
        static_cast<void>(leaf_element);
        return "";
#endif
    }

  private:

    std::runtime_error m;
};














































class parse_error : public exception
{
  public:









    template<typename BasicJsonType>
    static parse_error create(int id_, const position_t& pos, const std::string& what_arg, const BasicJsonType& context)
    {
        std::string w = exception::name("parse_error", id_) + "parse error" +
                        position_string(pos) + ": " + exception::diagnostics(context) + what_arg;
        return parse_error(id_, pos.chars_read_total, w.c_str());
    }

    template<typename BasicJsonType>
    static parse_error create(int id_, std::size_t byte_, const std::string& what_arg, const BasicJsonType& context)
    {
        std::string w = exception::name("parse_error", id_) + "parse error" +
                        (byte_ != 0 ? (" at byte " + std::to_string(byte_)) : "") +
                        ": " + exception::diagnostics(context) + what_arg;
        return parse_error(id_, byte_, w.c_str());
    }










    const std::size_t byte;

  private:
    parse_error(int id_, std::size_t byte_, const char* what_arg)
        : exception(id_, what_arg), byte(byte_) {}

    static std::string position_string(const position_t& pos)
    {
        return " at line " + std::to_string(pos.lines_read + 1) +
               ", column " + std::to_string(pos.chars_read_current_line);
    }
};






































class invalid_iterator : public exception
{
  public:
    template<typename BasicJsonType>
    static invalid_iterator create(int id_, const std::string& what_arg, const BasicJsonType& context)
    {
        std::string w = exception::name("invalid_iterator", id_) + exception::diagnostics(context) + what_arg;
        return invalid_iterator(id_, w.c_str());
    }

  private:
    JSON_HEDLEY_NON_NULL(3)
    invalid_iterator(int id_, const char* what_arg)
        : exception(id_, what_arg) {}
};








































class type_error : public exception
{
  public:
    template<typename BasicJsonType>
    static type_error create(int id_, const std::string& what_arg, const BasicJsonType& context)
    {
        std::string w = exception::name("type_error", id_) + exception::diagnostics(context) + what_arg;
        return type_error(id_, w.c_str());
    }

  private:
    JSON_HEDLEY_NON_NULL(3)
    type_error(int id_, const char* what_arg) : exception(id_, what_arg) {}
};


































class out_of_range : public exception
{
  public:
    template<typename BasicJsonType>
    static out_of_range create(int id_, const std::string& what_arg, const BasicJsonType& context)
    {
        std::string w = exception::name("out_of_range", id_) + exception::diagnostics(context) + what_arg;
        return out_of_range(id_, w.c_str());
    }

  private:
    JSON_HEDLEY_NON_NULL(3)
    out_of_range(int id_, const char* what_arg) : exception(id_, what_arg) {}
};

























class other_error : public exception
{
  public:
    template<typename BasicJsonType>
    static other_error create(int id_, const std::string& what_arg, const BasicJsonType& context)
    {
        std::string w = exception::name("other_error", id_) + exception::diagnostics(context) + what_arg;
        return other_error(id_, w.c_str());
    }

  private:
    JSON_HEDLEY_NON_NULL(3)
    other_error(int id_, const char* what_arg) : exception(id_, what_arg) {}
};
}
}
