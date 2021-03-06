#!/usr/bin/python
#snippet plugin
import juci_to_python_api as juci, inspect

def initPlugin():
    juci.addMenuElement("Snippet")
    juci.addSubMenuElement("SnippetMenu", #name of parent menu
                           "Insert snippet", #menu description
                           "insertSnippet()", #function to execute
                           inspect.getfile(inspect.currentframe()), #plugin path
                           "<control><alt>space")
snippets = {}

snippets["for"] = """\
for(int i=0; i<v.size(); i++) {
  // std::cout << v[i] << std::endl;
  // Write code here
}
"""

snippets["if"] = """\
if(true) {
    // Write code here
}
"""

snippets["ifelse"] = """\
if(false) {
    // Write code here
} else {
    // Write code here
}
"""

snippets["while"] = """\
while(condition) {
    // Write code here
}
"""

snippets["main"] = """\
int main(int argc, char *argv[]) {
    //Do something
}
"""

snippets["hello"] = """\
#include <iostream>

int main(int argc, char *argv[]) {
    std::cout << "Hello, world! << std::endl;
}
"""

def getSnippet(word):    
    try:
        output = snippets[word]
    except KeyError:
        output = word
    return output

def insertSnippet():
    theWord=juci.getWord()
    output=getSnippet(theWord)
    juci.replaceWord(output)


