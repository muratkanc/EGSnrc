/*
###############################################################################
#
#  EGSnrc egs++ radionuclide source
#  Copyright (C) 2015 National Research Council Canada
#
#  This file is part of EGSnrc.
#
#  EGSnrc is free software: you can redistribute it and/or modify it under
#  the terms of the GNU Affero General Public License as published by the
#  Free Software Foundation, either version 3 of the License, or (at your
#  option) any later version.
#
#  EGSnrc is distributed in the hope that it will be useful, but WITHOUT ANY
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
#  FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for
#  more details.
#
#  You should have received a copy of the GNU Affero General Public License
#  along with EGSnrc. If not, see <http://www.gnu.org/licenses/>.
#
###############################################################################
#
#  Author:          Reid Townson, 2016
#
#  Contributors:
#
###############################################################################
*/


/*! \file egs_radionuclide_source.cpp
 *  \brief A radionuclide source
 *  \RT
 */

#include "egs_radionuclide_source.h"
#include "egs_input.h"
#include "egs_math.h"
#include "egs_application.h"

EGS_RadionuclideSource::EGS_RadionuclideSource(EGS_Input *input,
        EGS_ObjectFactory *f) : EGS_BaseSource(input,f), shape(0),
    geom(0), regions(0), nrs(0), min_theta(0), max_theta(M_PI),
    min_phi(0), max_phi(2*M_PI), gc(IncludeAll), q_allowed(0), decays(0),
    activity(0) {

    int err;
    vector<int> tmp_q;
    err = input->getInput("charge", tmp_q);
    if (!err) {
        if (std::find(q_allowed.begin(), q_allowed.end(), -1) != q_allowed.end()
                && std::find(q_allowed.begin(), q_allowed.end(), 0) != q_allowed.end()
                && std::find(q_allowed.begin(), q_allowed.end(), 1) != q_allowed.end()
                && std::find(q_allowed.begin(), q_allowed.end(), 2) != q_allowed.end()
           ) {
            q_allowAll = true;
        }
        else {
            q_allowAll = false;
        }
        q_allowed = tmp_q;
    }
    else {
        q_allowAll = true;
        q_allowed.push_back(-1);
        q_allowed.push_back(1);
        q_allowed.push_back(0);
        q_allowed.push_back(2);
    }

    // Create the decay spectra
    count = 0;
    Emax = 0;
    unsigned int i = 0;
    EGS_Float spectrumWeightTotal = 0;
    while (input->getInputItem("spectrum")) {

        decays.push_back(EGS_BaseSpectrum::createSpectrum(input));

        // If spectrum creation failed skip to the next spectrum block
        if (!decays[i]) {
            decays.pop_back();
            continue;
        }

        EGS_Float spectrumMaxE = decays[i]->maxEnergy();
        if (spectrumMaxE > Emax) {
            Emax = spectrumMaxE;
        }

        spectrumWeightTotal += decays[i]->getSpectrumWeight();

        ++i;
    }
    if (decays.size() < 1) {
        egsWarning("EGS_RadionuclideSource: Error: No spectrum was defined\n");
        return;
    }

    // Normalize the spectrum weights
    for (i=0; i<decays.size(); ++i) {
        decays[i]->setSpectrumWeight(
            decays[i]->getSpectrumWeight() / spectrumWeightTotal);

        if (i > 0) {
            decays[i]->setSpectrumWeight(
                decays[i]->getSpectrumWeight() +
                decays[i-1]->getSpectrumWeight());
        }
    }

    // Get the activity
    EGS_Float tmp_A;
    err = input->getInput("activity", tmp_A);
    if (!err) {
        activity = tmp_A;
    }
    else {
        activity = 1;
    }
    egsInformation("EGS_RadionuclideSource: Activity [disintegrations/s]: %e\n",
                   activity);

    // Calculate the duration of the experiment
    // Based on ncase and activity
    EGS_Application *app = EGS_Application::activeApplication();
    EGS_Input *inp = app->getInput();
    EGS_Input *irc = 0;
    double ncase_double = 0;
    if (inp) {
        irc = inp->getInputItem("run control");

        EGS_Input *icontrol = irc->getInputItem("run control");
        if (!icontrol) {
            egsWarning("EGS_RadionuclideSource: no 'run control' "
                       "input to determine 'ncase'\n");
        }

        err = icontrol->getInput("number of histories", ncase_double);
        if (err) {
            err = icontrol->getInput("ncase", ncase_double);
            if (err) {
                egsWarning("EGS_RadionuclideSource: missing/wrong 'ncase' or "
                           "'number of histories' input\n");
            }
        }
    }
    else {
        egsWarning("EGS_RadionuclideSource: no 'run control' "
                   "input to determine 'ncase'\n");
    }

    double Tmax = ncase_double / activity;
    egsInformation("EGS_RadionuclideSource: Duration of experiment [s]: %e\n",
                   Tmax);
    for (i=0; i<decays.size(); ++i) {
        decays[i]->setMaximumTime(Tmax);
    }

    // Create the shape for source emissions
    vector<EGS_Float> pos;
    EGS_Input *ishape = input->takeInputItem("shape");
    if (ishape) {
        shape = EGS_BaseShape::createShape(ishape);
        delete ishape;
    }
    if (!shape) {
        string sname;
        err = input->getInput("shape name",sname);
        if (err)
            egsWarning("EGS_RadionuclideSource: missing/wrong inline shape "
                       "definition and missing wrong 'shape name' input\n");
        else {
            shape = EGS_BaseShape::getShape(sname);
            if (!shape) egsWarning("EGS_RadionuclideSource: a shape named %s"
                                       " does not exist\n");
        }
    }
    string geom_name;
    err = input->getInput("geometry",geom_name);
    if (!err) {
        geom = EGS_BaseGeometry::getGeometry(geom_name);
        if (!geom) egsWarning("EGS_RadionuclideSource: no geometry named %s\n",
                                  geom_name.c_str());
        else {
            vector<string> reg_options;
            reg_options.push_back("IncludeAll");
            reg_options.push_back("ExcludeAll");
            reg_options.push_back("IncludeSelected");
            reg_options.push_back("ExcludeSelected");
            gc = (GeometryConfinement) input->getInput("region "
                    "selection",reg_options,0);
            if (gc == IncludeSelected || gc == ExcludeSelected) {
                vector<int> regs;
                err = input->getInput("selected regions",regs);
                if (err || regs.size() < 1) {
                    egsWarning("EGS_RadionuclideSource: region selection %d "
                               "used  but no 'selected regions' input "
                               "found\n",gc);
                    gc = gc == IncludeSelected ? IncludeAll : ExcludeAll;
                    egsWarning(" using %d\n",gc);
                }
                nrs = regs.size();
                regions = new int [nrs];
                for (int j=0; j<nrs; j++) {
                    regions[j] = regs[j];
                }
            }
        }
    }
    EGS_Float tmp_theta;
    err = input->getInput("min theta", tmp_theta);
    if (!err) {
        min_theta = tmp_theta/180.0*M_PI;
    }

    err = input->getInput("max theta", tmp_theta);
    if (!err) {
        max_theta = tmp_theta/180.0*M_PI;
    }

    err = input->getInput("min phi", tmp_theta);
    if (!err) {
        min_phi = tmp_theta/180.0*M_PI;
    }

    err = input->getInput("max phi", tmp_theta);
    if (!err) {
        max_phi = tmp_theta/180.0*M_PI;
    }

    buf_1 = cos(min_theta);
    buf_2 = cos(max_theta);

    setUp();
}

EGS_I64 EGS_RadionuclideSource::getNextParticle(EGS_RandomGenerator *rndm, int
        &q, int &latch, EGS_Float &E, EGS_Float &wt, EGS_Vector &x, EGS_Vector
        &u) {

    unsigned int i = 0;
    if (decays.size() > 1) {
        // Sample a uniform random number
        EGS_Float uRand = rndm->getUniform();

        // Sample which spectrum to use
        for (i=0; i<decays.size(); ++i) {
            if (uRand < decays[i]->getSpectrumWeight()) {
                break;
            }
        }
    }

    for (EGS_I64 j=0; j<1e6; ++j) {

        E = decays[i]->sampleEnergy(rndm);

        if (E < 1e-10) {
            continue;
        }

        q = decays[i]->getCharge();

        // Check if the charge is allowed
        // If so, break out of the loop and keep the particle
        // Otherwise the loop will continue generating particles until
        // one matches the q_allowed criteria
        if (q_allowAll || std::find(q_allowed.begin(), q_allowed.end(), q) != q_allowed.end()) {
            break;
        }
    }

    time = decays[i]->getTime();
    ishower = decays[i]->getShowerIndex();

    getPositionDirection(rndm,x,u,wt);
    latch = 0;

    return ++count;
}

void EGS_RadionuclideSource::setUp() {
    otype = "EGS_RadionuclideSource";
    if (!isValid()) {
        description = "Invalid radionuclide source";
    }
    else {
        description = "Radionuclide source from a shape of type ";
        description += shape->getObjectType();
        description += " with:";
        if (std::find(q_allowed.begin(), q_allowed.end(), -1) !=
                q_allowed.end()) {
            description += " electrons";
        }
        if (std::find(q_allowed.begin(), q_allowed.end(), 0) != q_allowed.end()) {
            description += " photons";
        }
        if (std::find(q_allowed.begin(), q_allowed.end(), 1) != q_allowed.end()) {
            description += " positrons";
        }
        if (std::find(q_allowed.begin(), q_allowed.end(), 1) != q_allowed.end()) {
            description += " alphas";
        }

        if (geom) {
            geom->ref();
        }
    }
}

extern "C" {

    EGS_RADIONUCLIDE_SOURCE_EXPORT EGS_BaseSource *createSource(EGS_Input
            *input, EGS_ObjectFactory *f) {
        return
            createSourceTemplate<EGS_RadionuclideSource>(input,f,"radionuclide "
                    "source");
    }

}
