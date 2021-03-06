/*
* Parser Functions
* (C) 1999-2007 Jack Lloyd
*
* Distributed under the terms of the Botan license
*/

#include <botan/parsing.h>
#include <botan/exceptn.h>
#include <botan/charset.h>
#include <botan/loadstor.h>

namespace Botan {

/*
* Convert a string into an integer
*/
u32bit to_u32bit(const std::string& number)
   {
   u32bit n = 0;

   for(std::string::const_iterator j = number.begin(); j != number.end(); ++j)
      {
      const u32bit OVERFLOW_MARK = 0xFFFFFFFF / 10;

      byte digit = Charset::char2digit(*j);

      if((n > OVERFLOW_MARK) || (n == OVERFLOW_MARK && digit > 5))
         throw Decoding_Error("to_u32bit: Integer overflow");
      n *= 10;
      n += digit;
      }
   return n;
   }

/*
* Convert an integer into a string
*/
std::string to_string(u64bit n, u32bit min_len)
   {
   std::string lenstr;
   if(n)
      {
      while(n > 0)
         {
         lenstr = Charset::digit2char(n % 10) + lenstr;
         n /= 10;
         }
      }
   else
      lenstr = "0";

   while(lenstr.size() < min_len)
      lenstr = "0" + lenstr;

   return lenstr;
   }

/*
* Convert a string into a time duration
*/
u32bit timespec_to_u32bit(const std::string& timespec)
   {
   if(timespec == "")
      return 0;

   const char suffix = timespec[timespec.size()-1];
   std::string value = timespec.substr(0, timespec.size()-1);

   u32bit scale = 1;

   if(Charset::is_digit(suffix))
      value += suffix;
   else if(suffix == 's')
      scale = 1;
   else if(suffix == 'm')
      scale = 60;
   else if(suffix == 'h')
      scale = 60 * 60;
   else if(suffix == 'd')
      scale = 24 * 60 * 60;
   else if(suffix == 'y')
      scale = 365 * 24 * 60 * 60;
   else
      throw Decoding_Error("timespec_to_u32bit: Bad input " + timespec);

   return scale * to_u32bit(value);
   }

/*
* Parse a SCAN-style algorithm name
*/
std::vector<std::string> parse_algorithm_name(const std::string& namex)
   {
   if(namex.find('(') == std::string::npos &&
      namex.find(')') == std::string::npos)
      return std::vector<std::string>(1, namex);

   std::string name = namex, substring;
   std::vector<std::string> elems;
   u32bit level = 0;

   elems.push_back(name.substr(0, name.find('(')));
   name = name.substr(name.find('('));

   for(std::string::const_iterator j = name.begin(); j != name.end(); ++j)
      {
      char c = *j;

      if(c == '(')
         ++level;
      if(c == ')')
         {
         if(level == 1 && j == name.end() - 1)
            {
            if(elems.size() == 1)
               elems.push_back(substring.substr(1));
            else
               elems.push_back(substring);
            return elems;
            }

         if(level == 0 || (level == 1 && j != name.end() - 1))
            throw Invalid_Algorithm_Name(namex);
         --level;
         }

      if(c == ',' && level == 1)
         {
         if(elems.size() == 1)
            elems.push_back(substring.substr(1));
         else
            elems.push_back(substring);
         substring.clear();
         }
      else
         substring += c;
      }

   if(substring != "")
      throw Invalid_Algorithm_Name(namex);

   return elems;
   }

/*
* Split the string on slashes
*/
std::vector<std::string> split_on(const std::string& str, char delim)
   {
   std::vector<std::string> elems;
   if(str == "") return elems;

   std::string substr;
   for(std::string::const_iterator j = str.begin(); j != str.end(); ++j)
      {
      if(*j == delim)
         {
         if(substr != "")
            elems.push_back(substr);
         substr.clear();
         }
      else
         substr += *j;
      }

   if(substr == "")
      throw Format_Error("Unable to split string: " + str);
   elems.push_back(substr);

   return elems;
   }

/*
* Parse an ASN.1 OID string
*/
std::vector<u32bit> parse_asn1_oid(const std::string& oid)
   {
   std::string substring;
   std::vector<u32bit> oid_elems;

   for(std::string::const_iterator j = oid.begin(); j != oid.end(); ++j)
      {
      char c = *j;

      if(c == '.')
         {
         if(substring == "")
            throw Invalid_OID(oid);
         oid_elems.push_back(to_u32bit(substring));
         substring.clear();
         }
      else
         substring += c;
      }

   if(substring == "")
      throw Invalid_OID(oid);
   oid_elems.push_back(to_u32bit(substring));

   if(oid_elems.size() < 2)
      throw Invalid_OID(oid);

   return oid_elems;
   }

/*
* X.500 String Comparison
*/
bool x500_name_cmp(const std::string& name1, const std::string& name2)
   {
   std::string::const_iterator p1 = name1.begin();
   std::string::const_iterator p2 = name2.begin();

   while((p1 != name1.end()) && Charset::is_space(*p1)) ++p1;
   while((p2 != name2.end()) && Charset::is_space(*p2)) ++p2;

   while(p1 != name1.end() && p2 != name2.end())
      {
      if(Charset::is_space(*p1))
         {
         if(!Charset::is_space(*p2))
            return false;

         while((p1 != name1.end()) && Charset::is_space(*p1)) ++p1;
         while((p2 != name2.end()) && Charset::is_space(*p2)) ++p2;

         if(p1 == name1.end() && p2 == name2.end())
            return true;
         }

      if(!Charset::caseless_cmp(*p1, *p2))
         return false;
      ++p1;
      ++p2;
      }

   while((p1 != name1.end()) && Charset::is_space(*p1)) ++p1;
   while((p2 != name2.end()) && Charset::is_space(*p2)) ++p2;

   if((p1 != name1.end()) || (p2 != name2.end()))
      return false;
   return true;
   }

/*
* Convert a decimal-dotted string to binary IP
*/
u32bit string_to_ipv4(const std::string& str)
   {
   std::vector<std::string> parts = split_on(str, '.');

   if(parts.size() != 4)
      throw Decoding_Error("Invalid IP string " + str);

   u32bit ip = 0;

   for(size_t j = 0; j != parts.size(); j++)
      {
      u32bit octet = to_u32bit(parts[j]);

      if(octet > 255)
         throw Decoding_Error("Invalid IP string " + str);

      ip = (ip << 8) | (octet & 0xFF);
      }

   return ip;
   }

/*
* Convert an IP address to decimal-dotted string
*/
std::string ipv4_to_string(u32bit ip)
   {
   std::string str;

   for(size_t j = 0; j != sizeof(ip); j++)
      {
      if(j)
         str += ".";
      str += to_string(get_byte(j, ip));
      }

   return str;
   }

}
