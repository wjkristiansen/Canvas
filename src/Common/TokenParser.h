#pragma once

#include <string>
#include <vector>
#include <regex>
#include <memory>

namespace Canvas
{
    //------------------------------------------------------------------------------------------------
    // Parses a string into whitespace-delimited tokens.
    // Substrings inside single or double quotes are treated as single tokens with quotes stripped.
    // Only outermost quotes are considered for nested quotes.
    // Manages internal storage to provide stable const char* pointers.
    //------------------------------------------------------------------------------------------------
    class CTokenParser
    {
        std::vector<std::unique_ptr<char[]>> m_storage;
        std::vector<const char*> m_tokens;

        void ParseImpl(const std::string& input)
        {
            // Regex pattern breakdown:
            // ("[^"]*")         - Double-quoted string (capture everything between quotes)
            // |                 - OR
            // ('[^']*')         - Single-quoted string (capture everything between quotes)
            // |                 - OR
            // (\S+)             - Non-whitespace sequence (unquoted token)
            std::regex tokenRegex(R"(("[^"]*")|('[^']*')|(\S+))");

            auto begin = std::sregex_iterator(input.begin(), input.end(), tokenRegex);
            auto end = std::sregex_iterator();

            for (auto it = begin; it != end; ++it)
            {
                std::smatch match = *it;
                std::string token = match.str();

                // Strip quotes if present (outermost only)
                if ((token.front() == '"' && token.back() == '"') ||
                    (token.front() == '\'' && token.back() == '\''))
                {
                    if (token.size() >= 2)
                    {
                        token = token.substr(1, token.size() - 2);
                    }
                }

                // Allocate storage for the token string
                size_t size = token.size() + 1; // +1 for null terminator
                auto buffer = std::make_unique<char[]>(size);
                memcpy(buffer.get(), token.c_str(), size);

                // Store the pointer before moving ownership
                m_tokens.push_back(buffer.get());
                m_storage.push_back(std::move(buffer));
            }
        }

    public:
        CTokenParser() = default;

        // Parse a C-string into tokens
        explicit CTokenParser(const char* szInput)
        {
            if (szInput)
            {
                ParseImpl(std::string(szInput));
            }
        }

        // Parse a std::string into tokens
        explicit CTokenParser(const std::string& input)
        {
            ParseImpl(input);
        }

        // Get the number of parsed tokens
        size_t GetTokenCount() const
        {
            return m_tokens.size();
        }

        // Get the array of token pointers (argv-style)
        const char* const* GetTokens() const
        {
            return m_tokens.data();
        }

        // Get a specific token by index
        const char* GetToken(size_t index) const
        {
            return index < m_tokens.size() ? m_tokens[index] : nullptr;
        }

        // Array subscript operator for convenient access
        const char* operator[](size_t index) const
        {
            return GetToken(index);
        }
    };

} // namespace Canvas
