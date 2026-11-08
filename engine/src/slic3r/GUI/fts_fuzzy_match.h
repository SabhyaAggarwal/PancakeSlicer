// LICENSE


#ifndef FTS_FUZZY_MATCH_H
#define FTS_FUZZY_MATCH_H


#include <cstdint>
#include <ctype.h>
#include <cwctype>
#include <cstring>

#include <cstdio>

#include "../Utils/ASCIIFolding.hpp"

namespace fts {
	using 						char_type 	= wchar_t;
	using 						pos_type  	= uint16_t;
	static constexpr pos_type 	stopper 	= pos_type(-1);
	static constexpr int 		max_matches = 255;

    static bool fuzzy_match(char_type const * pattern, char_type const * str, int & outScore);
    static bool fuzzy_match(char_type const * pattern, char_type const * str, int & outScore, pos_type * matches);
}

#ifdef FTS_FUZZY_MATCH_IMPLEMENTATION
namespace fts {

    namespace fuzzy_internal {
        static bool fuzzy_match_recursive(const char_type * pattern, const char_type * str, int & outScore, const char_type * const strBegin,          
            pos_type const * srcMatches,  pos_type * newMatches, int nextMatch, 
            int recursionCount, const int recursionLimit);
        static void copy_matches(pos_type * dst, pos_type const* src);
    }

    [[maybe_unused]] static bool fuzzy_match(char_type const * pattern, char_type const * str, int & outScore) {
        pos_type matches[max_matches + 1];
        matches[0] = stopper;
        return fuzzy_match(pattern, str, outScore, matches);
    }

    [[maybe_unused]] static bool fuzzy_match(char_type const * pattern, char_type const * str, int & outScore, pos_type * matches) {
        int recursionCount = 0;
        static constexpr int recursionLimit = 10;
        return fuzzy_internal::fuzzy_match_recursive(pattern, str, outScore, str, nullptr, matches, 0, recursionCount, recursionLimit);
    }

    static bool fuzzy_internal::fuzzy_match_recursive(
    	const char_type * 		pattern, 
    	const char_type * 		str, 
    	int & 					outScore, 
        const char_type * const	strBegin, 
        pos_type const * 		srcMatches,
        pos_type * 				matches,
        int 					nextMatch,
        int				        recursionCount, 
        const int				recursionLimit)
    {
        if (++ recursionCount >= recursionLimit)
            return false;

        if (*pattern == '\0' || *str == '\0')
            return false;

        bool recursiveMatch = false;
        pos_type bestRecursiveMatches[max_matches + 1];
        int bestRecursiveScore = 0;

        bool first_match = true;
        while (*pattern != '\0' && *str != '\0') {

        	int  num_matched  = std::towlower(*pattern) == std::towlower(*str) ? 1 : 0;

            if (! num_matched) {
                wchar_t tmp[4];
                wchar_t *end = Slic3r::fold_to_ascii(*str, tmp);
                wchar_t *c = tmp;
                for (const wchar_t* d = pattern; c != end && *d != 0 && std::towlower(*c) == std::towlower(*d); ++c, ++d);
                if (c == end) {
        			num_matched = end - tmp;
        		}
	        }
            
            if (num_matched) {

                if (nextMatch + num_matched > max_matches)
                    return false;

                if (first_match && srcMatches) {
                    memcpy(matches, srcMatches, sizeof(pos_type) * (nextMatch + 1));
                    first_match = false;
                }

                pos_type recursiveMatches[max_matches + 1];
                int recursiveScore;
                if (fuzzy_match_recursive(pattern, str + 1, recursiveScore, strBegin, matches, recursiveMatches, nextMatch, recursionCount, recursionLimit)) {
                    
                    if (!recursiveMatch || recursiveScore > bestRecursiveScore) {
                    	copy_matches(bestRecursiveMatches, recursiveMatches);
                		bestRecursiveScore = recursiveScore;
                    }
                    recursiveMatch = true;
                }

                matches[nextMatch++] = (pos_type)(str - strBegin);
                matches[nextMatch] = stopper;
                pattern += num_matched;
            } 
            ++str;
        }

        bool matched = *pattern == '\0';

        if (matched) {
            static constexpr int sequential_bonus = 75;
            static constexpr int separator_bonus = 10;
            static constexpr int first_letter_bonus = -30;

            static constexpr int leading_letter_penalty = -1;
            static constexpr int max_leading_letter_penalty = -15;
            static constexpr int unmatched_letter_penalty = -1;
            while (*str != '\0')
                ++str;

            outScore = 100;

            const char_type *group_start = strBegin + matches[0];
            for (const char_type *c = group_start; c >= strBegin && *c != ':'; -- c)
            	if (*c != ' ' && *c != '\t')
            		group_start = c;

            outScore += matches[0] == int(group_start - strBegin) ?
            	first_letter_bonus :
            	std::max((matches[0] - int(group_start - strBegin)) * leading_letter_penalty, max_leading_letter_penalty);

            outScore += (int(str - group_start) - nextMatch) * unmatched_letter_penalty;

            int sequential_state = sequential_bonus;
            for (int i = 0; i < nextMatch; ++i) {
                pos_type currIdx = matches[i];

                if (currIdx > 0) {
                    if (i > 0 && currIdx == matches[i - 1] + 1) {
                        outScore += sequential_state;
                    	sequential_state = std::min(5 * sequential_bonus, sequential_state + sequential_state / 3);
                    } else {
                    	sequential_state = sequential_bonus;
                    }
					char_type prev = strBegin[currIdx - 1];
                    if (prev == '_' || prev == ' ')
                        outScore += separator_bonus;
                }
            }
        }

        if (recursiveMatch && (!matched || bestRecursiveScore > outScore)) {
            copy_matches(matches, bestRecursiveMatches);
            outScore = bestRecursiveScore;
            return true;
        }
        else if (matched) {
            return true;
        }
        else {
            return false;
        }
    }

    static void fuzzy_internal::copy_matches(pos_type * dst, pos_type const* src)
    {
        while (*src != stopper)
            *dst++ = *src++;
        *dst = stopper;
    }

}

#endif

#endif
