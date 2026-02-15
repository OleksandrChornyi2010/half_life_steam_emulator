//
// Created by home on 07.02.26.
//

#ifndef GOLDBERG_EMULATOR_VDF_PARSER_H
#define GOLDBERG_EMULATOR_VDF_PARSER_H

#include "base.h"
#include <regex>

struct VDFNode {
    std::string name;
    std::map<std::string, std::string> values;
    std::map<std::string, VDFNode> children;
    bool hasChild(const std::string& key) const;
    bool hasValue(const std::string& key) const;
};

class VDFParser {
public:
    static VDFNode parse(const std::string& filename);
    static void write(const std::string& filename, const VDFNode& node);
private:
    static void writeRecursive(std::ofstream& file, const VDFNode& node, int depth);
    static void parseRecursive(const std::string& content, size_t& pos, VDFNode& currentNode);
};

#endif //GOLDBERG_EMULATOR_VDF_PARSER_H

