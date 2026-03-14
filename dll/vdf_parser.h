/* Copyright (C) 2026 OleksandrChornyi2010 (SaNNa)
   This file is part of the half_life_steam_emulator

   The half_life_steam_emulator is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 3 of the License, or (at your option) any later version.

   The half_life_steam_emulator is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the half_life_steam_emulator; if not, see
   <http://www.gnu.org/licenses/>.  */

#ifndef VDF_PARSER_INCLUDE
#define VDF_PARSER_INCLUDE

#include "base.h"
#include <regex>

struct VDFNode {
    std::string name;
    std::map<std::string, std::string> values;
    std::map<std::string, VDFNode> children;
    bool hasChild(const std::string &key) const;
    bool hasValue(const std::string &key) const;
};

class VDFParser {
  public:
    static VDFNode parse(const std::string &filename);
    static void write(const std::string &filename, const VDFNode &node);

  private:
    static void writeRecursive(std::ofstream &file, const VDFNode &node, int depth);
    static void parseRecursive(const std::string &content, size_t &pos, VDFNode &currentNode);
};

#endif // VDF_PARSER_INCLUDE
