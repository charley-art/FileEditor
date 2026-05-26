#ifndef NCDIALECTS_H
#define NCDIALECTS_H

#include "ncparser.h"

namespace nc {

DialectProfile makeLynucEdmProfile();
const DialectProfile &lynucEdmProfile();

DialectProfile makeSampleRelaxedProfile();
const DialectProfile &sampleRelaxedProfile();

} // namespace nc

#endif // NCDIALECTS_H
