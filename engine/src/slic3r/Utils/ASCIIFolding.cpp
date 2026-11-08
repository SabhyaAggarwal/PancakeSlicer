#include "ASCIIFolding.hpp"

#include <iterator>
#include <cstddef>
#include <stdio.h>
#include <string.h>
#include <locale>
#include <boost/locale/encoding_utf.hpp>
#include <regex>
#include <string>

namespace Slic3r {

wchar_t* fold_to_ascii(wchar_t c, wchar_t *out)
{
	if (c < 0x080) {
	    *out ++ = c;
	} else {
		switch (c) {
		case L'\u00C0':
		case L'\u00C1':
		case L'\u00C2':
		case L'\u00C3':
		case L'\u00C4':
		case L'\u00C5':
		case L'\u0100':
		case L'\u0102':
		case L'\u0104':
		case L'\u018F':
		case L'\u01CD':
		case L'\u01DE':
		case L'\u01E0':
		case L'\u01FA':
		case L'\u0200':
		case L'\u0202':
		case L'\u0226':
		case L'\u023A':
		case L'\u1D00':
		case L'\u1E00':
		case L'\u1EA0':
		case L'\u1EA2':
		case L'\u1EA4':
		case L'\u1EA6':
		case L'\u1EA8':
		case L'\u1EAA':
		case L'\u1EAC':
		case L'\u1EAE':
		case L'\u1EB0':
		case L'\u1EB2':
		case L'\u1EB4':
		case L'\u1EB6':
		case L'\u24B6':
		case L'\uFF21':
		    *out ++ = 'A';
		    break;
		case L'\u00E0':
		case L'\u00E1':
		case L'\u00E2':
		case L'\u00E3':
		case L'\u00E4':
		case L'\u00E5':
		case L'\u0101':
		case L'\u0103':
		case L'\u0105':
		case L'\u01CE':
		case L'\u01DF':
		case L'\u01E1':
		case L'\u01FB':
		case L'\u0201':
		case L'\u0203':
		case L'\u0227':
		case L'\u0250':
		case L'\u0259':
		case L'\u025A':
		case L'\u1D8F':
		case L'\u1D95':
		case L'\u1E01':
		case L'\u1E9A':
		case L'\u1EA1':
		case L'\u1EA3':
		case L'\u1EA5':
		case L'\u1EA7':
		case L'\u1EA9':
		case L'\u1EAB':
		case L'\u1EAD':
		case L'\u1EAF':
		case L'\u1EB1':
		case L'\u1EB3':
		case L'\u1EB5':
		case L'\u1EB7':
		case L'\u2090':
		case L'\u2094':
		case L'\u24D0':
		case L'\u2C65':
		case L'\u2C6F':
		case L'\uFF41':
		    *out ++ = 'a';
		    break;
		case L'\uA732':
		    *out ++ = 'A';
		    *out ++ = 'A';
		    break;
		case L'\u00C6':
		case L'\u01E2':
		case L'\u01FC':
		case L'\u1D01':
		    *out ++ = 'A';
		    *out ++ = 'E';
		    break;
		case L'\uA734':
		    *out ++ = 'A';                    
		    *out ++ = 'O';
		    break;
		case L'\uA736':
		    *out ++ = 'A';
		    *out ++ = 'U';
		    break;
		case L'\uA738':
		case L'\uA73A':
		    *out ++ = 'A';
		    *out ++ = 'V';
		    break;
		case L'\uA73C':
		    *out ++ = 'A';
		    *out ++ = 'Y';
		    break;
		case L'\u249C':
		    *out ++ = '(';
		    *out ++ = 'a';
		    *out ++ = ')';
		    break;
		case L'\uA733':
		    *out ++ = 'a';
		    *out ++ = 'a';
		    break;
		case L'\u00E6':
		case L'\u01E3':
		case L'\u01FD':
		case L'\u1D02':
		    *out ++ = 'a';
		    *out ++ = 'e';
		    break;
		case L'\uA735':
		    *out ++ = 'a';
		    *out ++ = 'o';
		    break;
		case L'\uA737':
		    *out ++ = 'a';
		    *out ++ = 'u';
		    break;
		case L'\uA739':
		case L'\uA73B':
		    *out ++ = 'a';
		    *out ++ = 'v';
		    break;
		case L'\uA73D':
		    *out ++ = 'a';
		    *out ++ = 'y';
		    break;
		case L'\u0181':
		case L'\u0182':
		case L'\u0243':
		case L'\u0299':
		case L'\u1D03':
		case L'\u1E02':
		case L'\u1E04':
		case L'\u1E06':
		case L'\u24B7':
		case L'\uFF22':
		    *out ++ = 'B';
		    break;
		case L'\u0180':
		case L'\u0183':
		case L'\u0253':
		case L'\u1D6C':
		case L'\u1D80':
		case L'\u1E03':
		case L'\u1E05':
		case L'\u1E07':
		case L'\u24D1':
		case L'\uFF42':
		    *out ++ = 'b';
		    break;
		case L'\u249D':
		    *out ++ = '(';                    
		    *out ++ = 'b';
		    *out ++ = ')';
		    break;
		case L'\u00C7':
		case L'\u0106':
		case L'\u0108':
		case L'\u010A':
		case L'\u010C':
		case L'\u0187':
		case L'\u023B':
		case L'\u0297':
		case L'\u1D04':
		case L'\u1E08':
		case L'\u24B8':
		case L'\uFF23':
		    *out ++ = 'C';
		    break;
		case L'\u00E7':
		case L'\u0107':
		case L'\u0109':
		case L'\u010B':
		case L'\u010D':
		case L'\u0188':
		case L'\u023C':
		case L'\u0255':
		case L'\u1E09':
		case L'\u2184':
		case L'\u24D2':
		case L'\uA73E':
		case L'\uA73F':
		case L'\uFF43':
		    *out ++ = 'c';
		    break;
		case L'\u249E':
		    *out ++ = '(';
		    *out ++ = 'c';
		    *out ++ = ')';
		    break;
		case L'\u00D0':
		case L'\u010E':
		case L'\u0110':
		case L'\u0189':
		case L'\u018A':
		case L'\u018B':
		case L'\u1D05':
		case L'\u1D06':
		case L'\u1E0A':
		case L'\u1E0C':
		case L'\u1E0E':
		case L'\u1E10':
		case L'\u1E12':
		case L'\u24B9':
		case L'\uA779':
		case L'\uFF24':
		    *out ++ = 'D';
		    break;
		case L'\u00F0':
		case L'\u010F':
		case L'\u0111':
		case L'\u018C':
		case L'\u0221':
		case L'\u0256':
		case L'\u0257':
		case L'\u1D6D':
		case L'\u1D81':
		case L'\u1D91':
		case L'\u1E0B':
		case L'\u1E0D':
		case L'\u1E0F':
		case L'\u1E11':
		case L'\u1E13':
		case L'\u24D3':
		case L'\uA77A':
		case L'\uFF44':
		    *out ++ = 'd';
		    break;
		case L'\u01C4':
		case L'\u01F1':
		    *out ++ = 'D';
		    *out ++ = 'Z';
		    break;
		case L'\u01C5':
		case L'\u01F2':
		    *out ++ = 'D';
		    *out ++ = 'z';
		    break;
		case L'\u249F':
		    *out ++ = '(';
		    *out ++ = 'd';
		    *out ++ = ')';
		    break;
		case L'\u0238':
		    *out ++ = 'd';
		    *out ++ = 'b';
		    break;
		case L'\u01C6':
		case L'\u01F3':
		case L'\u02A3':
		case L'\u02A5':
		    *out ++ = 'd';
		    *out ++ = 'z';
		    break;
		case L'\u00C8':
		case L'\u00C9':
		case L'\u00CA':
		case L'\u00CB':
		case L'\u0112':
		case L'\u0114':
		case L'\u0116':
		case L'\u0118':
		case L'\u011A':
		case L'\u018E':
		case L'\u0190':
		case L'\u0204':
		case L'\u0206':
		case L'\u0228':
		case L'\u0246':
		case L'\u1D07':
		case L'\u1E14':
		case L'\u1E16':
		case L'\u1E18':
		case L'\u1E1A':
		case L'\u1E1C':
		case L'\u1EB8':
		case L'\u1EBA':
		case L'\u1EBC':
		case L'\u1EBE':
		case L'\u1EC0':
		case L'\u1EC2':
		case L'\u1EC4':
		case L'\u1EC6':
		case L'\u24BA':
		case L'\u2C7B':
		case L'\uFF25':
		    *out ++ = 'E';
		    break;
		case L'\u00E8':
		case L'\u00E9':
		case L'\u00EA':
		case L'\u00EB':
		case L'\u0113':
		case L'\u0115':
		case L'\u0117':
		case L'\u0119':
		case L'\u011B':
		case L'\u01DD':
		case L'\u0205':
		case L'\u0207':
		case L'\u0229':
		case L'\u0247':
		case L'\u0258':
		case L'\u025B':
		case L'\u025C':
		case L'\u025D':
		case L'\u025E':
		case L'\u029A':
		case L'\u1D08':
		case L'\u1D92':
		case L'\u1D93':
		case L'\u1D94':
		case L'\u1E15':
		case L'\u1E17':
		case L'\u1E19':
		case L'\u1E1B':
		case L'\u1E1D':
		case L'\u1EB9':
		case L'\u1EBB':
		case L'\u1EBD':
		case L'\u1EBF':
		case L'\u1EC1':
		case L'\u1EC3':
		case L'\u1EC5':
		case L'\u1EC7':
		case L'\u2091':
		case L'\u24D4':
		case L'\u2C78':
		case L'\uFF45':
		    *out ++ = 'e';
		    break;
		case L'\u24A0':
		    *out ++ = '(';
		    *out ++ = 'e';
		    *out ++ = ')';
		    break;
		case L'\u0191':
		case L'\u1E1E':
		case L'\u24BB':
		case L'\uA730':
		case L'\uA77B':
		case L'\uA7FB':
		case L'\uFF26':
		    *out ++ = 'F';
		    break;
		case L'\u0192':
		case L'\u1D6E':
		case L'\u1D82':
		case L'\u1E1F':
		case L'\u1E9B':
		case L'\u24D5':
		case L'\uA77C':
		case L'\uFF46':
		    *out ++ = 'f';
		    break;
		case L'\u24A1':
		    *out ++ = '(';
		    *out ++ = 'f';
		    *out ++ = ')';
		    break;
		case L'\uFB00':
		    *out ++ = 'f';
		    *out ++ = 'f';
		    break;
		case L'\uFB03':
		    *out ++ = 'f';
		    *out ++ = 'f';
		    *out ++ = 'i';
		    break;
		case L'\uFB04':
		    *out ++ = 'f';
		    *out ++ = 'f';
		    *out ++ = 'l';
		    break;
		case L'\uFB01':
		    *out ++ = 'f';
		    *out ++ = 'i';
		    break;
		case L'\uFB02':
		    *out ++ = 'f';
		    *out ++ = 'l';
		    break;
		case L'\u011C':
		case L'\u011E':
		case L'\u0120':
		case L'\u0122':
		case L'\u0193':
		case L'\u01E4':
		case L'\u01E5':
		case L'\u01E6':
		case L'\u01E7':
		case L'\u01F4':
		case L'\u0262':
		case L'\u029B':
		case L'\u1E20':
		case L'\u24BC':
		case L'\uA77D':
		case L'\uA77E':
		case L'\uFF27':
		    *out ++ = 'G';
		    break;
		case L'\u011D':
		case L'\u011F':
		case L'\u0121':
		case L'\u0123':
		case L'\u01F5':
		case L'\u0260':
		case L'\u0261':
		case L'\u1D77':
		case L'\u1D79':
		case L'\u1D83':
		case L'\u1E21':
		case L'\u24D6':
		case L'\uA77F':
		case L'\uFF47':
		    *out ++ = 'g';
		    break;
		case L'\u24A2':
		    *out ++ = '(';
		    *out ++ = 'g';
		    *out ++ = ')';
		    break;
		case L'\u0124':
		case L'\u0126':
		case L'\u021E':
		case L'\u029C':
		case L'\u1E22':
		case L'\u1E24':
		case L'\u1E26':
		case L'\u1E28':
		case L'\u1E2A':
		case L'\u24BD':
		case L'\u2C67':
		case L'\u2C75':
		case L'\uFF28':
		    *out ++ = 'H';
		    break;
		case L'\u0125':
		case L'\u0127':
		case L'\u021F':
		case L'\u0265':
		case L'\u0266':
		case L'\u02AE':
		case L'\u02AF':
		case L'\u1E23':
		case L'\u1E25':
		case L'\u1E27':
		case L'\u1E29':
		case L'\u1E2B':
		case L'\u1E96':
		case L'\u24D7':
		case L'\u2C68':
		case L'\u2C76':
		case L'\uFF48':
		    *out ++ = 'h';
		    break;
		case L'\u01F6':
		    *out ++ = 'H';
		    *out ++ = 'V';
		    break;
		case L'\u24A3':
		    *out ++ = '(';
		    *out ++ = 'h';
		    *out ++ = ')';
		    break;
		case L'\u0195':
		    *out ++ = 'h';
		    *out ++ = 'v';
		    break;
		case L'\u00CC':
		case L'\u00CD':
		case L'\u00CE':
		case L'\u00CF':
		case L'\u0128':
		case L'\u012A':
		case L'\u012C':
		case L'\u012E':
		case L'\u0130':
		case L'\u0196':
		case L'\u0197':
		case L'\u01CF':
		case L'\u0208':
		case L'\u020A':
		case L'\u026A':
		case L'\u1D7B':
		case L'\u1E2C':
		case L'\u1E2E':
		case L'\u1EC8':
		case L'\u1ECA':
		case L'\u24BE':
		case L'\uA7FE':
		case L'\uFF29':
		    *out ++ = 'I';
		    break;
		case L'\u00EC':
		case L'\u00ED':
		case L'\u00EE':
		case L'\u00EF':
		case L'\u0129':
		case L'\u012B':
		case L'\u012D':
		case L'\u012F':
		case L'\u0131':
		case L'\u01D0':
		case L'\u0209':
		case L'\u020B':
		case L'\u0268':
		case L'\u1D09':
		case L'\u1D62':
		case L'\u1D7C':
		case L'\u1D96':
		case L'\u1E2D':
		case L'\u1E2F':
		case L'\u1EC9':
		case L'\u1ECB':
		case L'\u2071':
		case L'\u24D8':
		case L'\uFF49':
		    *out ++ = 'i';
		    break;
		case L'\u0132':
		    *out ++ = 'I';
		    *out ++ = 'J';
		    break;
		case L'\u24A4':
		    *out ++ = '(';
		    *out ++ = 'i';
		    *out ++ = ')';
		    break;
		case L'\u0133':
		    *out ++ = 'i';
		    *out ++ = 'j';
		    break;
		case L'\u0134':
		case L'\u0248':
		case L'\u1D0A':
		case L'\u24BF':
		case L'\uFF2A':
		    *out ++ = 'J';
		    break;
		case L'\u0135':
		case L'\u01F0':
		case L'\u0237':
		case L'\u0249':
		case L'\u025F':
		case L'\u0284':
		case L'\u029D':
		case L'\u24D9':
		case L'\u2C7C':
		case L'\uFF4A':
		    *out ++ = 'j';
		    break;
		case L'\u24A5':
		    *out ++ = '(';
		    *out ++ = 'j';
		    *out ++ = ')';
		    break;
		case L'\u0136':
		case L'\u0198':
		case L'\u01E8':
		case L'\u1D0B':
		case L'\u1E30':
		case L'\u1E32':
		case L'\u1E34':
		case L'\u24C0':
		case L'\u2C69':
		case L'\uA740':
		case L'\uA742':
		case L'\uA744':
		case L'\uFF2B':
		    *out ++ = 'K';
		    break;
		case L'\u0137':
		case L'\u0199':
		case L'\u01E9':
		case L'\u029E':
		case L'\u1D84':
		case L'\u1E31':
		case L'\u1E33':
		case L'\u1E35':
		case L'\u24DA':
		case L'\u2C6A':
		case L'\uA741':
		case L'\uA743':
		case L'\uA745':
		case L'\uFF4B':
		    *out ++ = 'k';
		    break;
		case L'\u24A6':
		    *out ++ = '(';
		    *out ++ = 'k';
		    *out ++ = ')';
		    break;
		case L'\u0139':
		case L'\u013B':
		case L'\u013D':
		case L'\u013F':
		case L'\u0141':
		case L'\u023D':
		case L'\u029F':
		case L'\u1D0C':
		case L'\u1E36':
		case L'\u1E38':
		case L'\u1E3A':
		case L'\u1E3C':
		case L'\u24C1':
		case L'\u2C60':
		case L'\u2C62':
		case L'\uA746':
		case L'\uA748':
		case L'\uA780':
		case L'\uFF2C':
		    *out ++ = 'L';
		    break;
		case L'\u013A':
		case L'\u013C':
		case L'\u013E':
		case L'\u0140':
		case L'\u0142':
		case L'\u019A':
		case L'\u0234':
		case L'\u026B':
		case L'\u026C':
		case L'\u026D':
		case L'\u1D85':
		case L'\u1E37':
		case L'\u1E39':
		case L'\u1E3B':
		case L'\u1E3D':
		case L'\u24DB':
		case L'\u2C61':
		case L'\uA747':
		case L'\uA749':
		case L'\uA781':
		case L'\uFF4C':
		    *out ++ = 'l';
		    break;
		case L'\u01C7':
		    *out ++ = 'L';
		    *out ++ = 'J';
		    break;
		case L'\u1EFA':
		    *out ++ = 'L';
		    *out ++ = 'L';
		    break;
		case L'\u01C8':
		    *out ++ = 'L';
		    *out ++ = 'j';
		    break;
		case L'\u24A7':
		    *out ++ = '(';
		    *out ++ = 'l';
		    *out ++ = ')';
		    break;
		case L'\u01C9':
		    *out ++ = 'l';
		    *out ++ = 'j';
		    break;
		case L'\u1EFB':
		    *out ++ = 'l';
		    *out ++ = 'l';
		    break;
		case L'\u02AA':
		    *out ++ = 'l';
		    *out ++ = 's';
		    break;
		case L'\u02AB':
		    *out ++ = 'l';
		    *out ++ = 'z';
		    break;
		case L'\u019C':
		case L'\u1D0D':
		case L'\u1E3E':
		case L'\u1E40':
		case L'\u1E42':
		case L'\u24C2':
		case L'\u2C6E':
		case L'\uA7FD':
		case L'\uA7FF':
		case L'\uFF2D':
		    *out ++ = 'M';
		    break;
		case L'\u026F':
		case L'\u0270':
		case L'\u0271':
		case L'\u1D6F':
		case L'\u1D86':
		case L'\u1E3F':
		case L'\u1E41':
		case L'\u1E43':
		case L'\u24DC':
		case L'\uFF4D':
		    *out ++ = 'm';
		    break;
		case L'\u24A8':
		    *out ++ = '(';
		    *out ++ = 'm';
		    *out ++ = ')';
		    break;
		case L'\u00D1':
		case L'\u0143':
		case L'\u0145':
		case L'\u0147':
		case L'\u014A':
		case L'\u019D':
		case L'\u01F8':
		case L'\u0220':
		case L'\u0274':
		case L'\u1D0E':
		case L'\u1E44':
		case L'\u1E46':
		case L'\u1E48':
		case L'\u1E4A':
		case L'\u24C3':
		case L'\uFF2E':
		    *out ++ = 'N';
		    break;
		case L'\u00F1':
		case L'\u0144':
		case L'\u0146':
		case L'\u0148':
		case L'\u0149':
		case L'\u014B':
		case L'\u019E':
		case L'\u01F9':
		case L'\u0235':
		case L'\u0272':
		case L'\u0273':
		case L'\u1D70':
		case L'\u1D87':
		case L'\u1E45':
		case L'\u1E47':
		case L'\u1E49':
		case L'\u1E4B':
		case L'\u207F':
		case L'\u24DD':
		case L'\uFF4E':
		    *out ++ = 'n';
		    break;
		case L'\u01CA':
		    *out ++ = 'N';
		    *out ++ = 'J';
		    break;
		case L'\u01CB':
		    *out ++ = 'N';
		    *out ++ = 'j';
		    break;
		case L'\u24A9':
		    *out ++ = '(';
		    *out ++ = 'n';
		    *out ++ = ')';
		    break;
		case L'\u01CC':
		    *out ++ = 'n';
		    *out ++ = 'j';
		    break;
		case L'\u00D2':
		case L'\u00D3':
		case L'\u00D4':
		case L'\u00D5':
		case L'\u00D6':
		case L'\u00D8':
		case L'\u014C':
		case L'\u014E':
		case L'\u0150':
		case L'\u0186':
		case L'\u019F':
		case L'\u01A0':
		case L'\u01D1':
		case L'\u01EA':
		case L'\u01EC':
		case L'\u01FE':
		case L'\u020C':
		case L'\u020E':
		case L'\u022A':
		case L'\u022C':
		case L'\u022E':
		case L'\u0230':
		case L'\u1D0F':
		case L'\u1D10':
		case L'\u1E4C':
		case L'\u1E4E':
		case L'\u1E50':
		case L'\u1E52':
		case L'\u1ECC':
		case L'\u1ECE':
		case L'\u1ED0':
		case L'\u1ED2':
		case L'\u1ED4':
		case L'\u1ED6':
		case L'\u1ED8':
		case L'\u1EDA':
		case L'\u1EDC':
		case L'\u1EDE':
		case L'\u1EE0':
		case L'\u1EE2':
		case L'\u24C4':
		case L'\uA74A':
		case L'\uA74C':
		case L'\uFF2F':
		    *out ++ = 'O';
		    break;
		case L'\u00F2':
		case L'\u00F3':
		case L'\u00F4':
		case L'\u00F5':
		case L'\u00F6':
		case L'\u00F8':
		case L'\u014D':
		case L'\u014F':
		case L'\u0151':
		case L'\u01A1':
		case L'\u01D2':
		case L'\u01EB':
		case L'\u01ED':
		case L'\u01FF':
		case L'\u020D':
		case L'\u020F':
		case L'\u022B':
		case L'\u022D':
		case L'\u022F':
		case L'\u0231':
		case L'\u0254':
		case L'\u0275':
		case L'\u1D16':
		case L'\u1D17':
		case L'\u1D97':
		case L'\u1E4D':
		case L'\u1E4F':
		case L'\u1E51':
		case L'\u1E53':
		case L'\u1ECD':
		case L'\u1ECF':
		case L'\u1ED1':
		case L'\u1ED3':
		case L'\u1ED5':
		case L'\u1ED7':
		case L'\u1ED9':
		case L'\u1EDB':
		case L'\u1EDD':
		case L'\u1EDF':
		case L'\u1EE1':
		case L'\u1EE3':
		case L'\u2092':
		case L'\u24DE':
		case L'\u2C7A':
		case L'\uA74B':
		case L'\uA74D':
		case L'\uFF4F':
		    *out ++ = 'o';
		    break;
		case L'\u0152':
		case L'\u0276':
		    *out ++ = 'O';
		    *out ++ = 'E';
		    break;
		case L'\uA74E':
		    *out ++ = 'O';
		    *out ++ = 'O';
		    break;
		case L'\u0222':
		case L'\u1D15':
		    *out ++ = 'O';
		    *out ++ = 'U';
		    break;
		case L'\u24AA':
		    *out ++ = '(';
		    *out ++ = 'o';
		    *out ++ = ')';
		    break;
		case L'\u0153':
		case L'\u1D14':
		    *out ++ = 'o';
		    *out ++ = 'e';
		    break;
		case L'\uA74F':
		    *out ++ = 'o';
		    *out ++ = 'o';
		    break;
		case L'\u0223':
		    *out ++ = 'o';
		    *out ++ = 'u';
		    break;
		case L'\u01A4':
		case L'\u1D18':
		case L'\u1E54':
		case L'\u1E56':
		case L'\u24C5':
		case L'\u2C63':
		case L'\uA750':
		case L'\uA752':
		case L'\uA754':
		case L'\uFF30':
		    *out ++ = 'P';
		    break;
		case L'\u01A5':
		case L'\u1D71':
		case L'\u1D7D':
		case L'\u1D88':
		case L'\u1E55':
		case L'\u1E57':
		case L'\u24DF':
		case L'\uA751':
		case L'\uA753':
		case L'\uA755':
		case L'\uA7FC':
		case L'\uFF50':
		    *out ++ = 'p';
		    break;
		case L'\u24AB':
		    *out ++ = '(';
		    *out ++ = 'p';
		    *out ++ = ')';
		    break;
		case L'\u024A':
		case L'\u24C6':
		case L'\uA756':
		case L'\uA758':
		case L'\uFF31':
		    *out ++ = 'Q';
		    break;
		case L'\u0138':
		case L'\u024B':
		case L'\u02A0':
		case L'\u24E0':
		case L'\uA757':
		case L'\uA759':
		case L'\uFF51':
		    *out ++ = 'q';
		    break;
		case L'\u24AC':
		    *out ++ = '(';
		    *out ++ = 'q';
		    *out ++ = ')';
		    break;
		case L'\u0239':
		    *out ++ = 'q';
		    *out ++ = 'p';
		    break;
		case L'\u0154':
		case L'\u0156':
		case L'\u0158':
		case L'\u0210':
		case L'\u0212':
		case L'\u024C':
		case L'\u0280':
		case L'\u0281':
		case L'\u1D19':
		case L'\u1D1A':
		case L'\u1E58':
		case L'\u1E5A':
		case L'\u1E5C':
		case L'\u1E5E':
		case L'\u24C7':
		case L'\u2C64':
		case L'\uA75A':
		case L'\uA782':
		case L'\uFF32':
		    *out ++ = 'R';
		    break;
		case L'\u0155':
		case L'\u0157':
		case L'\u0159':
		case L'\u0211':
		case L'\u0213':
		case L'\u024D':
		case L'\u027C':
		case L'\u027D':
		case L'\u027E':
		case L'\u027F':
		case L'\u1D63':
		case L'\u1D72':
		case L'\u1D73':
		case L'\u1D89':
		case L'\u1E59':
		case L'\u1E5B':
		case L'\u1E5D':
		case L'\u1E5F':
		case L'\u24E1':
		case L'\uA75B':
		case L'\uA783':
		case L'\uFF52':
		    *out ++ = 'r';
		    break;
		case L'\u24AD':
		    *out ++ = '(';
		    *out ++ = 'r';
		    *out ++ = ')';
		    break;
		case L'\u015A':
		case L'\u015C':
		case L'\u015E':
		case L'\u0160':
		case L'\u0218':
		case L'\u1E60':
		case L'\u1E62':
		case L'\u1E64':
		case L'\u1E66':
		case L'\u1E68':
		case L'\u24C8':
		case L'\uA731':
		case L'\uA785':
		case L'\uFF33':
		    *out ++ = 'S';
		    break;
		case L'\u015B':
		case L'\u015D':
		case L'\u015F':
		case L'\u0161':
		case L'\u017F':
		case L'\u0219':
		case L'\u023F':
		case L'\u0282':
		case L'\u1D74':
		case L'\u1D8A':
		case L'\u1E61':
		case L'\u1E63':
		case L'\u1E65':
		case L'\u1E67':
		case L'\u1E69':
		case L'\u1E9C':
		case L'\u1E9D':
		case L'\u24E2':
		case L'\uA784':
		case L'\uFF53':
		    *out ++ = 's';
		    break;
		case L'\u1E9E':
		    *out ++ = 'S';
		    *out ++ = 'S';
		    break;
		case L'\u24AE':
		    *out ++ = '(';
		    *out ++ = 's';
		    *out ++ = ')';
		    break;
		case L'\u00DF':
		    *out ++ = 's';
		    *out ++ = 's';
		    break;
		case L'\uFB06':
		    *out ++ = 's';
		    *out ++ = 't';
		    break;
		case L'\u0162':
		case L'\u0164':
		case L'\u0166':
		case L'\u01AC':
		case L'\u01AE':
		case L'\u021A':
		case L'\u023E':
		case L'\u1D1B':
		case L'\u1E6A':
		case L'\u1E6C':
		case L'\u1E6E':
		case L'\u1E70':
		case L'\u24C9':
		case L'\uA786':
		case L'\uFF34':
		    *out ++ = 'T';
		    break;
		case L'\u0163':
		case L'\u0165':
		case L'\u0167':
		case L'\u01AB':
		case L'\u01AD':
		case L'\u021B':
		case L'\u0236':
		case L'\u0287':
		case L'\u0288':
		case L'\u1D75':
		case L'\u1E6B':
		case L'\u1E6D':
		case L'\u1E6F':
		case L'\u1E71':
		case L'\u1E97':
		case L'\u24E3':
		case L'\u2C66':
		case L'\uFF54':
		    *out ++ = 't';
		    break;
		case L'\u00DE':
		case L'\uA766':
		    *out ++ = 'T';
		    *out ++ = 'H';
		    break;
		case L'\uA728':
		    *out ++ = 'T';
		    *out ++ = 'Z';
		    break;
		case L'\u24AF':
		    *out ++ = '(';
		    *out ++ = 't';
		    *out ++ = ')';
		    break;
		case L'\u02A8':
		    *out ++ = 't';
		    *out ++ = 'c';
		    break;
		case L'\u00FE':
		case L'\u1D7A':
		case L'\uA767':
		    *out ++ = 't';
		    *out ++ = 'h';
		    break;
		case L'\u02A6':
		    *out ++ = 't';
		    *out ++ = 's';
		    break;
		case L'\uA729':
		    *out ++ = 't';
		    *out ++ = 'z';
		    break;
		case L'\u00D9':
		case L'\u00DA':
		case L'\u00DB':
		case L'\u00DC':
		case L'\u0168':
		case L'\u016A':
		case L'\u016C':
		case L'\u016E':
		case L'\u0170':
		case L'\u0172':
		case L'\u01AF':
		case L'\u01D3':
		case L'\u01D5':
		case L'\u01D7':
		case L'\u01D9':
		case L'\u01DB':
		case L'\u0214':
		case L'\u0216':
		case L'\u0244':
		case L'\u1D1C':
		case L'\u1D7E':
		case L'\u1E72':
		case L'\u1E74':
		case L'\u1E76':
		case L'\u1E78':
		case L'\u1E7A':
		case L'\u1EE4':
		case L'\u1EE6':
		case L'\u1EE8':
		case L'\u1EEA':
		case L'\u1EEC':
		case L'\u1EEE':
		case L'\u1EF0':
		case L'\u24CA':
		case L'\uFF35':
		    *out ++ = 'U';
		    break;
		case L'\u00F9':
		case L'\u00FA':
		case L'\u00FB':
		case L'\u00FC':
		case L'\u0169':
		case L'\u016B':
		case L'\u016D':
		case L'\u016F':
		case L'\u0171':
		case L'\u0173':
		case L'\u01B0':
		case L'\u01D4':
		case L'\u01D6':
		case L'\u01D8':
		case L'\u01DA':
		case L'\u01DC':
		case L'\u0215':
		case L'\u0217':
		case L'\u0289':
		case L'\u1D64':
		case L'\u1D99':
		case L'\u1E73':
		case L'\u1E75':
		case L'\u1E77':
		case L'\u1E79':
		case L'\u1E7B':
		case L'\u1EE5':
		case L'\u1EE7':
		case L'\u1EE9':
		case L'\u1EEB':
		case L'\u1EED':
		case L'\u1EEF':
		case L'\u1EF1':
		case L'\u24E4':
		case L'\uFF55':
		    *out ++ = 'u';
		    break;
		case L'\u24B0':
		    *out ++ = '(';
		    *out ++ = 'u';
		    *out ++ = ')';
		    break;
		case L'\u1D6B':
		    *out ++ = 'u';
		    *out ++ = 'e';
		    break;
		case L'\u01B2':
		case L'\u0245':
		case L'\u1D20':
		case L'\u1E7C':
		case L'\u1E7E':
		case L'\u1EFC':
		case L'\u24CB':
		case L'\uA75E':
		case L'\uA768':
		case L'\uFF36':
		    *out ++ = 'V';
		    break;
		case L'\u028B':
		case L'\u028C':
		case L'\u1D65':
		case L'\u1D8C':
		case L'\u1E7D':
		case L'\u1E7F':
		case L'\u24E5':
		case L'\u2C71':
		case L'\u2C74':
		case L'\uA75F':
		case L'\uFF56':
		    *out ++ = 'v';
		    break;
		case L'\uA760':
		    *out ++ = 'V';
		    *out ++ = 'Y';
		    break;
		case L'\u24B1':
		    *out ++ = '(';
		    *out ++ = 'v';
		    *out ++ = ')';
		    break;
		case L'\uA761':
		    *out ++ = 'v';
		    *out ++ = 'y';
		    break;
		case L'\u0174':
		case L'\u01F7':
		case L'\u1D21':
		case L'\u1E80':
		case L'\u1E82':
		case L'\u1E84':
		case L'\u1E86':
		case L'\u1E88':
		case L'\u24CC':
		case L'\u2C72':
		case L'\uFF37':
		    *out ++ = 'W';
		    break;
		case L'\u0175':
		case L'\u01BF':
		case L'\u028D':
		case L'\u1E81':
		case L'\u1E83':
		case L'\u1E85':
		case L'\u1E87':
		case L'\u1E89':
		case L'\u1E98':
		case L'\u24E6':
		case L'\u2C73':
		case L'\uFF57':
		    *out ++ = 'w';
		    break;
		case L'\u24B2':
		    *out ++ = '(';
		    *out ++ = 'w';
		    *out ++ = ')';
		    break;
		case L'\u1E8A':
		case L'\u1E8C':
		case L'\u24CD':
		case L'\uFF38':
		    *out ++ = 'X';
		    break;
		case L'\u1D8D':
		case L'\u1E8B':
		case L'\u1E8D':
		case L'\u2093':
		case L'\u24E7':
		case L'\uFF58':
		    *out ++ = 'x';
		    break;
		case L'\u24B3':
		    *out ++ = '(';
		    *out ++ = 'x';
		    *out ++ = ')';
		    break;
		case L'\u00DD':
		case L'\u0176':
		case L'\u0178':
		case L'\u01B3':
		case L'\u0232':
		case L'\u024E':
		case L'\u028F':
		case L'\u1E8E':
		case L'\u1EF2':
		case L'\u1EF4':
		case L'\u1EF6':
		case L'\u1EF8':
		case L'\u1EFE':
		case L'\u24CE':
		case L'\uFF39':
		    *out ++ = 'Y';
		    break;
		case L'\u00FD':
		case L'\u00FF':
		case L'\u0177':
		case L'\u01B4':
		case L'\u0233':
		case L'\u024F':
		case L'\u028E':
		case L'\u1E8F':
		case L'\u1E99':
		case L'\u1EF3':
		case L'\u1EF5':
		case L'\u1EF7':
		case L'\u1EF9':
		case L'\u1EFF':
		case L'\u24E8':
		case L'\uFF59':
		    *out ++ = 'y';
		    break;
		case L'\u24B4':
		    *out ++ = '(';
		    *out ++ = 'y';
		    *out ++ = ')';
		    break;
		case L'\u0179':
		case L'\u017B':
		case L'\u017D':
		case L'\u01B5':
		case L'\u021C':
		case L'\u0224':
		case L'\u1D22':
		case L'\u1E90':
		case L'\u1E92':
		case L'\u1E94':
		case L'\u24CF':
		case L'\u2C6B':
		case L'\uA762':
		case L'\uFF3A':
		    *out ++ = 'Z';
		    break;
		case L'\u017A':
		case L'\u017C':
		case L'\u017E':
		case L'\u01B6':
		case L'\u021D':
		case L'\u0225':
		case L'\u0240':
		case L'\u0290':
		case L'\u0291':
		case L'\u1D76':
		case L'\u1D8E':
		case L'\u1E91':
		case L'\u1E93':
		case L'\u1E95':
		case L'\u24E9':
		case L'\u2C6C':
		case L'\uA763':
		case L'\uFF5A':
		    *out ++ = 'z';
		    break;
		case L'\u24B5':
		    *out ++ = '(';
		    *out ++ = 'z';
		    *out ++ = ')';
		    break;
		case L'\u2070':
		case L'\u2080':
		case L'\u24EA':
		case L'\u24FF':
		case L'\uFF10':
		    *out ++ = '0';
		    break;
		case L'\u00B9':
		case L'\u2081':
		case L'\u2460':
		case L'\u24F5':
		case L'\u2776':
		case L'\u2780':
		case L'\u278A':
		case L'\uFF11':
		    *out ++ = '1';
		    break;
		case L'\u2488':
		    *out ++ = '1';
		    *out ++ = '.';
		    break;
		case L'\u2474':
		    *out ++ = '(';
		    *out ++ = '1';
		    *out ++ = ')';
		    break;
		case L'\u00B2':
		case L'\u2082':
		case L'\u2461':
		case L'\u24F6':
		case L'\u2777':
		case L'\u2781':
		case L'\u278B':
		case L'\uFF12':
		    *out ++ = '2';
		    break;
		case L'\u2489':
		    *out ++ = '2';
		    *out ++ = '.';
		    break;
		case L'\u2475':
		    *out ++ = '(';
		    *out ++ = '2';
		    *out ++ = ')';
		    break;
		case L'\u00B3':
		case L'\u2083':
		case L'\u2462':
		case L'\u24F7':
		case L'\u2778':
		case L'\u2782':
		case L'\u278C':
		case L'\uFF13':
		    *out ++ = '3';
		    break;
		case L'\u248A':
		    *out ++ = '3';
		    *out ++ = '.';
		    break;
		case L'\u2476':
		    *out ++ = '(';
		    *out ++ = '3';
		    *out ++ = ')';
		    break;
		case L'\u2074':
		case L'\u2084':
		case L'\u2463':
		case L'\u24F8':
		case L'\u2779':
		case L'\u2783':
		case L'\u278D':
		case L'\uFF14':
		    *out ++ = '4';
		    break;
		case L'\u248B':
		    *out ++ = '4';
		    *out ++ = '.';
		    break;
		case L'\u2477':
		    *out ++ = '(';
		    *out ++ = '4';
		    *out ++ = ')';
		    break;
		case L'\u2075':
		case L'\u2085':
		case L'\u2464':
		case L'\u24F9':
		case L'\u277A':
		case L'\u2784':
		case L'\u278E':
		case L'\uFF15':
		    *out ++ = '5';
		    break;
		case L'\u248C':
		    *out ++ = '5';
		    *out ++ = '.';
		    break;
		case L'\u2478':
		    *out ++ = '(';
		    *out ++ = '5';
		    *out ++ = ')';
		    break;
		case L'\u2076':
		case L'\u2086':
		case L'\u2465':
		case L'\u24FA':
		case L'\u277B':
		case L'\u2785':
		case L'\u278F':
		case L'\uFF16':
		    *out ++ = '6';
		    break;
		case L'\u248D':
		    *out ++ = '6';
		    *out ++ = '.';
		    break;
		case L'\u2479':
		    *out ++ = '(';
		    *out ++ = '6';
		    *out ++ = ')';
		    break;
		case L'\u2077':
		case L'\u2087':
		case L'\u2466':
		case L'\u24FB':
		case L'\u277C':
		case L'\u2786':
		case L'\u2790':
		case L'\uFF17':
		    *out ++ = '7';
		    break;
		case L'\u248E':
		    *out ++ = '7';
		    *out ++ = '.';
		    break;
		case L'\u247A':
		    *out ++ = '(';
		    *out ++ = '7';
		    *out ++ = ')';
		    break;
		case L'\u2078':
		case L'\u2088':
		case L'\u2467':
		case L'\u24FC':
		case L'\u277D':
		case L'\u2787':
		case L'\u2791':
		case L'\uFF18':
		    *out ++ = '8';
		    break;
		case L'\u248F':
		    *out ++ = '8';
		    *out ++ = '.';
		    break;
		case L'\u247B':
		    *out ++ = '(';
		    *out ++ = '8';
		    *out ++ = ')';
		    break;
		case L'\u2079':
		case L'\u2089':
		case L'\u2468':
		case L'\u24FD':
		case L'\u277E':
		case L'\u2788':
		case L'\u2792':
		case L'\uFF19':
		    *out ++ = '9';
		    break;
		case L'\u2490':
		    *out ++ = '9';
		    *out ++ = '.';
		    break;
		case L'\u247C':
		    *out ++ = '(';
		    *out ++ = '9';
		    *out ++ = ')';
		    break;
		case L'\u2469':
		case L'\u24FE':
		case L'\u277F':
		case L'\u2789':
		case L'\u2793':
		    *out ++ = '1';
		    *out ++ = '0';
		    break;
		case L'\u2491':
		    *out ++ = '1';
		    *out ++ = '0';
		    *out ++ = '.';
		    break;
		case L'\u247D':
		    *out ++ = '(';
		    *out ++ = '1';
		    *out ++ = '0';
		    *out ++ = ')';
		    break;
		case L'\u246A':
		case L'\u24EB':
		    *out ++ = '1';
		    *out ++ = '1';
		    break;
		case L'\u2492':
		    *out ++ = '1';
		    *out ++ = '1';
		    *out ++ = '.';
		    break;
		case L'\u247E':
		    *out ++ = '(';
		    *out ++ = '1';
		    *out ++ = '1';
		    *out ++ = ')';
		    break;
		case L'\u246B':
		case L'\u24EC':
		    *out ++ = '1';
		    *out ++ = '2';
		    break;
		case L'\u2493':
		    *out ++ = '1';
		    *out ++ = '2';
		    *out ++ = '.';
		    break;
		case L'\u247F':
		    *out ++ = '(';
		    *out ++ = '1';
		    *out ++ = '2';
		    *out ++ = ')';
		    break;
		case L'\u246C':
		case L'\u24ED':
		    *out ++ = '1';
		    *out ++ = '3';
		    break;
		case L'\u2494':
		    *out ++ = '1';
		    *out ++ = '3';
		    *out ++ = '.';
		    break;
		case L'\u2480':
		    *out ++ = '(';
		    *out ++ = '1';
		    *out ++ = '3';
		    *out ++ = ')';
		    break;
		case L'\u246D':
		case L'\u24EE':
		    *out ++ = '1';
		    *out ++ = '4';
		    break;
		case L'\u2495':
		    *out ++ = '1';
		    *out ++ = '4';
		    *out ++ = '.';
		    break;
		case L'\u2481':
		    *out ++ = '(';
		    *out ++ = '1';
		    *out ++ = '4';
		    *out ++ = ')';
		    break;
		case L'\u246E':
		case L'\u24EF':
		    *out ++ = '1';
		    *out ++ = '5';
		    break;
		case L'\u2496':
		    *out ++ = '1';
		    *out ++ = '5';
		    *out ++ = '.';
		    break;
		case L'\u2482':
		    *out ++ = '(';
		    *out ++ = '1';
		    *out ++ = '5';
		    *out ++ = ')';
		    break;
		case L'\u246F':
		case L'\u24F0':
		    *out ++ = '1';
		    *out ++ = '6';
		    break;
		case L'\u2497':
		    *out ++ = '1';
		    *out ++ = '6';
		    *out ++ = '.';
		    break;
		case L'\u2483':
		    *out ++ = '(';
		    *out ++ = '1';
		    *out ++ = '6';
		    *out ++ = ')';
		    break;
		case L'\u2470':
		case L'\u24F1':
		    *out ++ = '1';
		    *out ++ = '7';
		    break;
		case L'\u2498':
		    *out ++ = '1';
		    *out ++ = '7';
		    *out ++ = '.';
		    break;
		case L'\u2484':
		    *out ++ = '(';
		    *out ++ = '1';
		    *out ++ = '7';
		    *out ++ = ')';
		    break;
		case L'\u2471':
		case L'\u24F2':
		    *out ++ = '1';
		    *out ++ = '8';
		    break;
		case L'\u2499':
		    *out ++ = '1';
		    *out ++ = '8';
		    *out ++ = '.';
		    break;
		case L'\u2485':
		    *out ++ = '(';
		    *out ++ = '1';
		    *out ++ = '8';
		    *out ++ = ')';
		    break;
		case L'\u2472':
		case L'\u24F3':
		    *out ++ = '1';
		    *out ++ = '9';
		    break;
		case L'\u249A':
		    *out ++ = '1';
		    *out ++ = '9';
		    *out ++ = '.';
		    break;
		case L'\u2486':
		    *out ++ = '(';
		    *out ++ = '1';
		    *out ++ = '9';
		    *out ++ = ')';
		    break;
		case L'\u2473':
		case L'\u24F4':
		    *out ++ = '2';
		    *out ++ = '0';
		    break;
		case L'\u249B':
		    *out ++ = '2';
		    *out ++ = '0';
		    *out ++ = '.';
		    break;
		case L'\u2487':
		    *out ++ = '(';
		    *out ++ = '2';
		    *out ++ = '0';
		    *out ++ = ')';
		    break;
		case L'\u00AB':
		case L'\u00BB':
		case L'\u201C':
		case L'\u201D':
		case L'\u201E':
		case L'\u2033':
		case L'\u2036':
		case L'\u275D':
		case L'\u275E':
		case L'\u276E':
		case L'\u276F':
		case L'\uFF02':
		    *out ++ = '"';
		    break;
		case L'\u2018':
		case L'\u2019':
		case L'\u201A':
		case L'\u201B':
		case L'\u2032':
		case L'\u2035':
		case L'\u2039':
		case L'\u203A':
		case L'\u275B':
		case L'\u275C':
		case L'\uFF07':
		    *out ++ = '\'';
		    break;
		case L'\u2010':
		case L'\u2011':
		case L'\u2012':
		case L'\u2013':
		case L'\u2014':
		case L'\u207B':
		case L'\u208B':
		case L'\uFF0D':
		    *out ++ = '-';
		    break;
		case L'\u2045':
		case L'\u2772':
		case L'\uFF3B':
		    *out ++ = '[';
		    break;
		case L'\u2046':
		case L'\u2773':
		case L'\uFF3D':
		    *out ++ = ']';
		    break;
		case L'\u207D':
		case L'\u208D':
		case L'\u2768':
		case L'\u276A':
		case L'\uFF08':
		    *out ++ = '(';
		    break;
		case L'\u2E28':
		    *out ++ = '(';
		    *out ++ = '(';
		    break;
		case L'\u207E':
		case L'\u208E':
		case L'\u2769':
		case L'\u276B':
		case L'\uFF09':
		    *out ++ = ')';
		    break;
		case L'\u2E29':
		    *out ++ = ')';
		    *out ++ = ')';
		    break;
		case L'\u276C':
		case L'\u2770':
		case L'\uFF1C':
		    *out ++ = '<';
		    break;
		case L'\u276D':
		case L'\u2771':
		case L'\uFF1E':
		    *out ++ = '>';
		    break;
		case L'\u2774':
		case L'\uFF5B':
		    *out ++ = '{';
		    break;
		case L'\u2775':
		case L'\uFF5D':
		    *out ++ = '}';
		    break;
		case L'\u207A':
		case L'\u208A':
		case L'\uFF0B':
		    *out ++ = '+';
		    break;
		case L'\u207C':
		case L'\u208C':
		case L'\uFF1D':
		    *out ++ = '=';
		    break;
		case L'\uFF01':
		    *out ++ = '!';
		    break;
		case L'\u203C':
		    *out ++ = '!';
		    *out ++ = '!';
		    break;
		case L'\u2049':
		    *out ++ = '!';
		    *out ++ = '?';
		    break;
		case L'\uFF03':
		    *out ++ = '#';
		    break;
		case L'\uFF04':
		    *out ++ = '$';
		    break;
		case L'\u2052':
		case L'\uFF05':
		    *out ++ = '%';
		    break;
		case L'\uFF06':
		    *out ++ = '&';
		    break;
		case L'\u204E':
		case L'\uFF0A':
		    *out ++ = '*';
		    break;
		case L'\uFF0C':
		    *out ++ = ',';
		    break;
		case L'\uFF0E':
		    *out ++ = '.';
		    break;
		case L'\u2044':
		case L'\uFF0F':
		    *out ++ = '/';
		    break;
		case L'\uFF1A':
		    *out ++ = ':';
		    break;
		case L'\u204F':
		case L'\uFF1B':
		    *out ++ = ';';
		    break;
		case L'\uFF1F':
		    *out ++ = '?';
		    break;
		case L'\u2047':
		    *out ++ = '?';
		    *out ++ = '?';
		    break;
		case L'\u2048':
		    *out ++ = '?';
		    *out ++ = '!';
		    break;
		case L'\uFF20':
		    *out ++ = '@';
		    break;
		case L'\uFF3C':
		    *out ++ = '\\';
		    break;
		case L'\u2038':
		case L'\uFF3E':
		    *out ++ = '^';
		    break;
		case L'\uFF3F':
		    *out ++ = '_';
		    break;
		case L'\u2053':
		case L'\uFF5E':
		    *out ++ = '~';
		    break;
		default:
            *out ++ = c;
		    break;
		}
	}

	return out;
}

static void fold_to_ascii(wchar_t c, std::back_insert_iterator<std::wstring>& out)
{
    wchar_t tmp[4];
    wchar_t *end = fold_to_ascii(c, tmp);
    for (wchar_t *it = tmp; it != end; ++ it)
        *out = *it;
}

std::string fold_utf8_to_ascii(const std::string &src, bool is_convert_for_filename)
{
	std::wstring wstr = boost::locale::conv::utf_to_utf<wchar_t>(src.c_str(), src.c_str() + src.size());
	std::wstring dst;
	dst.reserve(wstr.size());
	auto out = std::back_insert_iterator<std::wstring>(dst);
	for (wchar_t c : wstr)
		fold_to_ascii(c, out);
	if (is_convert_for_filename) {
		auto   dstStr = boost::locale::conv::utf_to_utf<char>(dst.c_str(), dst.c_str() + dst.size());

		std::size_t found = dstStr.find_last_of("/\\");
		if (found != std::string::npos) {
			std::string dir      = dstStr.substr(0, found);
			std::string filename = dstStr.substr(found + 1);
			std::regex reg("[\\\\/:*?\"<>|\\0]");
			std::string newFileName = regex_replace(filename, reg, "");
			dstStr  = dir + "\\" + newFileName;
        }
		dst = boost::locale::conv::utf_to_utf<wchar_t>(dstStr.c_str(), dstStr.c_str() + dstStr.size());
	}

	return boost::locale::conv::utf_to_utf<char>(dst.c_str(), dst.c_str() + dst.size());
}


}
