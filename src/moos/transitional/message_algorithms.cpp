// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Community contributors (see AUTHORS file)
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include <boost/foreach.hpp>

#include "goby/acomms/dccl.h"
#include "message.h"
#include "message_algorithms.h"
#include "message_val.h"

#include "goby/util/as.h"

goby::transitional::DCCLAlgorithmPerformer* goby::transitional::DCCLAlgorithmPerformer::inst_ = 0;

// singleton class, use this to get pointer
goby::transitional::DCCLAlgorithmPerformer*
goby::transitional::DCCLAlgorithmPerformer::getInstance()
{
    if (!inst_)
        inst_ = new DCCLAlgorithmPerformer();

    return (inst_);
}

void goby::transitional::DCCLAlgorithmPerformer::deleteInstance() { delete inst_; }

void goby::transitional::DCCLAlgorithmPerformer::algorithm(
    DCCLMessageVal& in, unsigned array_index, const std::string& algorithm,
    const std::map<std::string, std::vector<DCCLMessageVal> >& vals)
{
    if (in.empty())
        return;

    // algo_name:ref_variable_name1:ref_variable_name2...

    std::vector<std::string> ref_vars;
    std::string algorithm_deblanked = boost::erase_all_copy(algorithm, " ");
    boost::split(ref_vars, algorithm_deblanked, boost::is_any_of(":"));

    std::string alg;
    std::vector<DCCLMessageVal> tied_vals;

    for (std::vector<std::string>::size_type i = 1, n = ref_vars.size(); i < n; ++i)
    {
        std::map<std::string, std::vector<DCCLMessageVal> >::const_iterator it =
            vals.find(ref_vars[i]);
        if (it != vals.end())
        {
            if (array_index < it->second.size())
                tied_vals.push_back(it->second[array_index]);
            else
                tied_vals.push_back(it->second[0]);
        }
    }

    if (ref_vars.size() > 0)
        alg = ref_vars[0];

    run_algorithm(alg, in, tied_vals);
}

void goby::transitional::DCCLAlgorithmPerformer::run_algorithm(
    const std::string& algorithm, DCCLMessageVal& in, const std::vector<DCCLMessageVal>& ref)
{
    // short form for simple algorithms
    if (adv_map1_.count(algorithm))
    {
        adv_map1_.find(algorithm)->second(in);
    }
    // longer form for more demanding algorithms
    else if (adv_map2_.count(algorithm))
    {
        adv_map2_.find(algorithm)->second(in, ref);
    }
}

// check validity of algorithm name and references
void goby::transitional::DCCLAlgorithmPerformer::check_algorithm(const std::string& alg,
                                                                 const DCCLMessage& msg)
{
    if (alg.empty())
        return;

    std::vector<std::string> ref_vars;
    std::string algorithm_deblanked = boost::erase_all_copy(alg, " ");
    boost::split(ref_vars, algorithm_deblanked, boost::is_any_of(":"));

    // check if the algorithm exists
    // but ignore if no algorithms loaded (to use for testing tools)
    if ((adv_map1_.size() || adv_map2_.size()) && !adv_map1_.count(ref_vars.at(0)) &&
        !adv_map2_.count(ref_vars.at(0)))
        throw(goby::acomms::DCCLException("unknown algorithm defined: " + ref_vars.at(0)));

    for (std::vector<std::string>::size_type i = 1, n = ref_vars.size(); i < n; ++i)
    {
        bool ref_found = false;
        BOOST_FOREACH (const boost::shared_ptr<DCCLMessageVar> mv, msg.header_const())
        {
            if (ref_vars[i] == mv->name())
            {
                ref_found = true;
                break;
            }
        }

        BOOST_FOREACH (const boost::shared_ptr<DCCLMessageVar> mv, msg.layout_const())
        {
            if (ref_vars[i] == mv->name())
            {
                ref_found = true;
                break;
            }
        }

        if (!ref_found)
            throw(goby::acomms::DCCLException(
                std::string("no such reference message variable " + ref_vars.at(i) +
                            " used in algorithm: " + ref_vars.at(0))));
    }
}
