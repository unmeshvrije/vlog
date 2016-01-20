/*
   Copyright (C) 2015 Jacopo Urbani.

   This file is part of Vlog.

   Vlog is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 2 of the License, or
   (at your option) any later version.

   Vlog is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Vlog.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <vlog/qsqquery.h>

QSQQuery::QSQQuery(const Literal literal) : literal(literal) {
    //Calculate the positions to copy
    nPosToCopy = 0;
    nRepeatedVars = 0;

    uint8_t nExistingVars = 0;
    std::pair<uint8_t, uint8_t> existingVars[SIZETUPLE]; //value,pos

    for (uint8_t i = 0; i < (uint8_t) literal.getTupleSize(); ++i) {
        if (literal.getTermAtPos(i).isVariable()) {
            posToCopy[nPosToCopy++] = i;

            int8_t posRepeated = -1;
            if (nExistingVars > 0) {
                for (uint8_t j = 0; j < nExistingVars; ++j) {
                    if (literal.getTermAtPos(i).getId() == existingVars[j].first) {
                        posRepeated = j;
                        break;
                    }
                }
            }

            if (posRepeated == -1) {
                existingVars[nExistingVars++] = std::make_pair(literal.getTermAtPos(i).getId(), i);
            } else {
                repeatedVars[nRepeatedVars++] = std::make_pair(existingVars[posRepeated].second, i);
            }
        }
    }
}

std::string QSQQuery::tostring() {
    std::string out = std::string("[") + literal.tostring();
    out += std::string("nPosToCopy=") + std::to_string(nPosToCopy) + std::string("]");
    return out;
}