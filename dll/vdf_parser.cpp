//
// Created by home on 07.02.26.
//

#include "vdf_parser.h"

// Helper to check if a child exists
bool VDFNode::hasChild(const std::string &key) const {
    return children.find(key) != children.end();
}

// Helper to check if a value exists
bool VDFNode::hasValue(const std::string &key) const {
    return values.find(key) != values.end();
}

VDFNode VDFParser::parse(const std::string &filename) {
    Local_Storage::safe_create_file(filename);
    VDFNode root;
    root.name = "root";

    std::ifstream file(filename);

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    size_t pos = 0;
    parseRecursive(content, pos, root);
    return root;
}

void VDFParser::write(const std::string &filename, const VDFNode &node) {
    Local_Storage::safe_create_file(filename);

    std::ofstream file(filename);
    // The root node is a container, we write its children
    for (auto const &[name, child] : node.children) {
        writeRecursive(file, child, 0);
    }
    file.close();
}

void VDFParser::writeRecursive(std::ofstream &file, const VDFNode &node, int depth) {
    std::string indent(depth, '\t');
    file << indent << "\"" << node.name << "\"\n";
    file << indent << "{\n";

    for (auto const &[key, val] : node.values) {
        file << indent << "\t\"" << key << "\"\t\t\"" << val << "\"\n";
    }

    for (auto const &[name, child] : node.children) {
        writeRecursive(file, child, depth + 1);
    }

    file << indent << "}\n";
}

void VDFParser::parseRecursive(const std::string &content, size_t &pos, VDFNode &currentNode) {
    // Regex to match "key" "value" or "key" {
    std::regex tokenRegex(R"(\s*\"([^\"]+)\"(\s+\"([^\"]+)\"|(\s*\{)))");
    std::smatch match;

    while (pos < content.size()) {
        std::string searchArea = content.substr(pos);

        // Check for closing brace
        size_t endBrace = searchArea.find_first_not_of(" \t\r\n");
        if (endBrace != std::string::npos && searchArea[endBrace] == '}') {
            pos += endBrace + 1;
            return;
        }

        if (std::regex_search(searchArea, match, tokenRegex)) {
            std::string key = match[1].str();
            pos += match.position() + match.length();

            if (match[3].matched) {
                // Key-Value pair found
                currentNode.values[key] = match[3].str();
            } else if (match[4].matched) {
                // Start of a new block
                VDFNode child;
                child.name = key;
                parseRecursive(content, pos, child);
                currentNode.children[key] = child;
            }
        } else {
            break;
        }
    }
}