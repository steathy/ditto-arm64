#include <cstdio>
#include <cstring>
#include "tinyxml.h"
int main() {
    TiXmlDocument doc;
    doc.Parse("<audit target=\"ARM64\"><value>Ditto</value></audit>");
    if (doc.Error() || !doc.RootElement()) return 1;
    if (std::strcmp(doc.RootElement()->Attribute("target"), "ARM64")) return 2;
    std::puts("PASS: bundled TinyXML parses attributes and elements");
    return 0;
}
