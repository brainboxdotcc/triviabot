/************************************************************************************
 *
 * TriviaBot, The trivia bot for discord based on Fruitloopy Trivia for ChatSpike IRC
 *
 * Copyright 2004 Craig Edwards <support@brainbox.cc>
 *
 * Core based on Sporks, the Learning Discord Bot, Craig Edwards (c) 2019.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ************************************************************************************/

#include <iostream>
#include <string>
#include <locale>
#include <codecvt>
#include <algorithm>
#include "wlower.h"

/* Lowercases a utf8 string using unicode case-folding rules.
 * Note the special flag 'spanish_hack' that allows for lazy grammar in spanish only.
 * Lazy grammar lets a Spanish speaker answer 'papa' when they meant 'papá'
 * (The first means 'potatoes' and the second means 'father'!) This happens a lot amongst
 * younger players, so we allow this as a way to make things friendlier.
 */
std::string utf8lower(const std::string &input, bool spanish_hack)
{
	std::setlocale(LC_CTYPE, "en_US.UTF-8"); // the locale will be the UTF-8 enabled English

	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;

	std::wstring str = converter.from_bytes(input.c_str());
	for (std::wstring::iterator it = str.begin(); it != str.end(); ++it) {
		*it = towlower(*it);
		if (spanish_hack) {
			/* spanish_hack grammar rules on vowels with accents only, AEIOU! */
			if (*it == L'á') {
				*it = L'a';
			} else if (*it == L'é') {
				*it = L'e';
			} else if (*it == L'ó') {
				*it = L'o';
			} else if (*it == L'ú' || *it == L'ü') {
				*it = L'u';
			}
		}
	}
	return converter.to_bytes(str);
}

/* Counts the vowels and length of a unicode utf8 string. Vowels valid for cyrillic and latin languages */
std::pair<int, int> countvowel(const std::string &input)
{
	std::string i = utf8lower(input, true);
	std::setlocale(LC_CTYPE, "en_US.UTF-8");
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	std::wstring str = converter.from_bytes(i.c_str());
	int vowels = 0;
	int len = 0;
	for (std::wstring::iterator it = str.begin(); it != str.end(); ++it) {
		*it = towlower(*it);
		if (
			/* Latin alphabet vowels (with and without accent characters) */
			*it == L'á' || *it == L'é' || *it == L'ó' || *it == L'ú' || *it == L'ü' || *it == L'a' || *it == L'e' || *it == L'i' || *it == L'o' || *it == L'u'
			/* Cyrillic alphabet vowels */
			|| *it == L'е' || *it == L'о' || *it == L'а' || *it == L'э' || *it == L'ы' || *it == L'у' || *it == L'я' || *it == L'ё' || *it == L'ю' || *it == L'и'
		) {
			vowels++;
		}
		if (*it != L' ') {
			len++;
		}
	}
	return std::make_pair(vowels, len);
}

/* Shuffle and lowercase the contents of a utf8 string for use in srambled answer hints */
std::string utf8shuffle(const std::string &input)
{
	std::string i = utf8lower(input, false);
	std::setlocale(LC_CTYPE, "en_US.UTF-8");
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	std::wstring str = converter.from_bytes(i.c_str());
	std::random_shuffle(str.begin(), str.end());
	return converter.to_bytes(str);
}

/* Translates normal ascii text into a random jumble of cyrillic, runic, and other stuff that looks enough like english to be readable, but
 * can't be pasted into google, and will break selfbots that arent aware of it. Kind of like a captcha. Note that this won't protect against
 * selfbots forever and is more an anti-googling mechanism.
 */
std::string homoglyph(const std::string &input)
{
	std::wstring vowel_A(L"Ａ𝐴𝖠𝘈𝙰ΑАᎪᗅꓮ");
	std::wstring vowel_E(L"𝖤𝗘𝙴Ε𝛦𝝚ЕⴹᎬꓰ");
	std::wstring vowel_O(L"߀𝟢𝟶𝑂𝖮𝘖𝙾ΟОՕⵔ𐓂ꓳ𐐄");
	std::wstring vowel_o(L"൦๐໐𝑜𝗈𝘰𝚘ᴏᴑο𝜊оჿօ");

	std::random_shuffle(vowel_A.begin(), vowel_A.end());
	std::random_shuffle(vowel_E.begin(), vowel_E.end());
	std::random_shuffle(vowel_O.begin(), vowel_O.end());
	std::random_shuffle(vowel_o.begin(), vowel_o.end());

	std::wstring o;
	std::setlocale(LC_CTYPE, "en_US.UTF-8");
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	std::wstring str = converter.from_bytes(input.c_str());
	for (std::wstring::iterator it = str.begin(); it != str.end(); ++it) {
		switch (*it) {
			case L'1':	o += L'1';	break;
			case L'2':	o += L'2';	break;
			case L'3':	o += L'3';	break;
			case L'4':	o += L'4';	break;
			case L'5':	o += L'5';	break;
			case L'7':	o += L'7';	break;
			case L'8':	o += L'8';	break;
			case L'9':	o += L'9';	break;
			case L'-':	o += L'‐';	break;
			case L',':	o += L',';	break;
			case L'_':	o += L'_';	break;
			case L'a':	o += L'а';	break;
			case L'b':	o += L'Ь';	break;
			case L'c':	o += L'с';	break;
			case L'd':	o += L'ԁ';	break;
			case L'e':	o += L'е';	break;
			case L'f':	o += L'ғ';	break;
			case L'g':	o += L'ɡ';	break;
			case L'h':	o += L'һ';	break;
			case L'i':	o += L'і';	break;
			case L'j':	o += L'ј';	break;
			case L'k':	o += L'κ';	break;
			case L'l':	o += L'ⅼ';	break;
			case L'm':	o += L'ⅿ';	break;
			case L'n':	o += L'ո';	break;
			case L'o':	o += vowel_o[0];break;
			case L'p':	o += L'р';	break;
			case L'q':	o += L'ԛ';	break;
			case L'r':	o += L'г';	break;
			case L's':	o += L'ѕ';	break;
			case L't':	o += L'𝚝';	break;
			case L'u':	o += L'υ';	break;
			case L'v':	o += L'ⅴ';	break;
			case L'w':	o += L'ѡ';	break;
			case L'x':	o += L'х';	break;
			case L'y':	o += L'у';	break;
			case L'z':	o += L'z';	break;
			case L'A':	o += vowel_A[0];break;
			case L'B':	o += L'Β';	break;
			case L'C':	o += L'Ϲ';	break;
			case L'D':	o += L'Ⅾ';	break;
			case L'E':	o += vowel_E[0];break;
			case L'F':	o += L'Ғ';	break;
			case L'G':	o += L'Ԍ';	break;
			case L'H':	o += L'Η';	break;
			case L'I':	o += L'Ι';	break;
			case L'J':	o += L'Ј';	break;
			case L'K':	o += L'Κ';	break;
			case L'L':	o += L'Ⅼ';	break;
			case L'M':	o += L'Μ';	break;
			case L'N':	o += L'Ν';	break;
			case L'O':	o += vowel_O[0];break;
			case L'P':	o += L'Ρ';	break;
			case L'Q':	o += L'Ԛ';	break;
			case L'R':	o += L'R';	break;
			case L'S':	o += L'Ѕ';	break;
			case L'T':	o += L'⊤';	break;
			case L'U':	o += L'⋃';	break;
			case L'V':	o += L'Ⅴ';	break;
			case L'W':	o += L'W';	break;
			case L'X':	o += L'Χ';	break;
			case L'Y':	o += L'Ү';	break;
			case L'Z':	o += L'Ζ';	break;
			case L')':	o += L'❳';	break;
			case L'(':	o += L'❲';	break;
			default:	o += *it;	break;
		}
	}
	return converter.to_bytes(o);
}

size_t wlength(const std::string &input)
{
	std::setlocale(LC_CTYPE, "en_US.UTF-8");
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	return converter.from_bytes(input.c_str()).length();
}

std::string wfirst(const std::string &input)
{
	std::setlocale(LC_CTYPE, "en_US.UTF-8");
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	std::wstring str = converter.from_bytes(input.c_str());
	std::wstring r;
	std::wstring::const_iterator n = str.begin();
	r += *n;
	return converter.to_bytes(r);
}

std::string wlast(const std::string &input)
{
	std::setlocale(LC_CTYPE, "en_US.UTF-8");
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	std::wstring str = converter.from_bytes(input.c_str());
	std::wstring r;
	std::wstring::const_iterator n = str.end();
	--n;
	r += *n;
	return converter.to_bytes(r);
}

std::string removepunct(const std::string &input)
{
	std::setlocale(LC_CTYPE, "en_US.UTF-8"); // the locale will be the UTF-8 enabled English
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	std::wstring str = converter.from_bytes(input.c_str());
	std::wstring out;
	for (std::wstring::const_iterator c = str.begin(); c != str.end(); ++c) {
		if (!
			(*c == L',' || *c == L'.'   || *c == L':'  || *c == L'/'  ||
			 *c == L';' || *c == L'!'   || *c == L'?'  || *c == L'('  ||
			 *c == L'‘' || *c == L'’'   || *c == L'“'  || *c == L'”'  ||
			 *c == L'«' || *c == L'»'   || *c == L'‹'  || *c == L'›'  ||
			 *c == L'「' || *c == L'」' || *c == L'﹁' || *c == L'﹂' ||
			 *c == L'『' || *c == L'』' || *c == L'﹃' || *c == L'﹄' ||
			 *c == L'《' || *c == L'》' || *c == L'〈' || *c == L'〉' ||
			 *c == L')' || *c == L'-'   || *c == L'"'  || *c == L'\'' ||
			 *c == L'„' || *c == L'\r'  || *c == L'\n' || *c == L'\t' ||
			 *c == L'\v')
		) {
			out += *c;
		}
	}
	return converter.to_bytes(out);
}
