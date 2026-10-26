#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(ZipTies::modelZipTie);
	p->addModel(ZipTies::modelCablePegs);
}
