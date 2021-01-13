// Copyright 2011-2020:
//   GobySoft, LLC (2013-)
//   Massachusetts Institute of Technology (2007-2014)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
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

#ifndef GOBY_MOOS_MOOS_TRANSLATOR_H
#define GOBY_MOOS_MOOS_TRANSLATOR_H

#include "goby/moos/moos_header.h"
#include "moos_geodesy.h"
#include <map>
#include <set>
#include <string>

#include "dccl/dynamic_protobuf_manager.h"
#include "goby/moos/modem_id_convert.h"
#include "goby/moos/moos_protobuf_helpers.h"
#include "goby/moos/protobuf/translator.pb.h"
#include "goby/moos/transitional/message_algorithms.h"

namespace goby
{
namespace moos
{
void alg_power_to_dB(moos::transitional::DCCLMessageVal& val_to_mod);
void alg_dB_to_power(moos::transitional::DCCLMessageVal& val_to_mod);

// applied to "T" (temperature), references are "S" (salinity), then "D" (depth)
void alg_TSD_to_soundspeed(moos::transitional::DCCLMessageVal& val,
                           const std::vector<moos::transitional::DCCLMessageVal>& ref_vals);

// ref_vals subtracted from val
void alg_subtract(moos::transitional::DCCLMessageVal& val,
                  const std::vector<moos::transitional::DCCLMessageVal>& ref_vals);

// ref_vals added to val
void alg_add(moos::transitional::DCCLMessageVal& val,
             const std::vector<moos::transitional::DCCLMessageVal>& ref_vals);

void alg_angle_0_360(moos::transitional::DCCLMessageVal& angle);
void alg_angle_n180_180(moos::transitional::DCCLMessageVal& angle);

void alg_to_upper(moos::transitional::DCCLMessageVal& val_to_mod);
void alg_to_lower(moos::transitional::DCCLMessageVal& val_to_mod);

void alg_abs(moos::transitional::DCCLMessageVal& val_to_mod);
void alg_lat2hemisphere_initial(moos::transitional::DCCLMessageVal& val_to_mod);
void alg_lon2hemisphere_initial(moos::transitional::DCCLMessageVal& val_to_mod);

void alg_unix_time2nmea_time(moos::transitional::DCCLMessageVal& val_to_mod);

void alg_lat2nmea_lat(moos::transitional::DCCLMessageVal& val_to_mod);
void alg_lon2nmea_lon(moos::transitional::DCCLMessageVal& val_to_mod);

class MOOSTranslator
{
  public:
    MOOSTranslator(const goby::moos::protobuf::TranslatorEntry& entry =
                       goby::moos::protobuf::TranslatorEntry(),
                   double lat_origin = std::numeric_limits<double>::quiet_NaN(),
                   double lon_origin = std::numeric_limits<double>::quiet_NaN(),
                   const std::string& modem_id_lookup_path = "")
    {
        initialize(lat_origin, lon_origin, modem_id_lookup_path);
        if (entry.IsInitialized())
            add_entry(entry);
    }

    MOOSTranslator(
        const google::protobuf::RepeatedPtrField<goby::moos::protobuf::TranslatorEntry>& entries,
        double lat_origin = std::numeric_limits<double>::quiet_NaN(),
        double lon_origin = std::numeric_limits<double>::quiet_NaN(),
        const std::string& modem_id_lookup_path = "")
    {
        initialize(lat_origin, lon_origin, modem_id_lookup_path);
        add_entry(entries);
    }

    void clear_entry(const std::string& protobuf_name) { dictionary_.erase(protobuf_name); }

    void add_entry(const goby::moos::protobuf::TranslatorEntry& entry)
    {
        if (dictionary_.count(entry.protobuf_name()))
            throw(std::runtime_error("Duplicate translator entry for " + entry.protobuf_name()));
        dictionary_[entry.protobuf_name()] = entry;
    }

    void add_entry(const std::set<goby::moos::protobuf::TranslatorEntry>& entries)
    {
        for (const auto& entry : entries) { add_entry(entry); }
    }

    void add_entry(
        const google::protobuf::RepeatedPtrField<goby::moos::protobuf::TranslatorEntry>& entries)
    {
        for (const auto& entry : entries) { add_entry(entry); }
    }

    // ownership of returned pointer goes to caller (must use smart pointer or call delete)
    template <typename GoogleProtobufMessagePointer, class StringCMOOSMsgMap>
    GoogleProtobufMessagePointer moos_to_protobuf(const StringCMOOSMsgMap& moos_variables,
                                                  const std::string& protobuf_name);

    std::multimap<std::string, CMOOSMsg>
    protobuf_to_moos(const google::protobuf::Message& protobuf_msg);

    // advanced: writes to create, instead of publish!
    std::multimap<std::string, CMOOSMsg>
    protobuf_to_inverse_moos(const google::protobuf::Message& protobuf_msg);

    const std::map<std::string, goby::moos::protobuf::TranslatorEntry>& dictionary() const
    {
        return dictionary_;
    }

    static CMOOSMsg
    make_moos_msg(const std::string& var, const std::string& str, bool is_binary,
                  goby::moos::protobuf::TranslatorEntry::ParserSerializerTechnique technique,
                  const std::string& pb_name)
    {
        if (is_binary)
        {
            CMOOSMsg moos_msg(MOOS_NOTIFY, var, str.size(), str.data());
            moos_msg.SetSourceAux(
                pb_name + ":" +
                goby::moos::protobuf::TranslatorEntry::ParserSerializerTechnique_Name(technique));
            return moos_msg;
        }
        else
        {
            try
            {
                double return_double = boost::lexical_cast<double>(str);
                return CMOOSMsg(MOOS_NOTIFY, var, return_double);
            }
            catch (boost::bad_lexical_cast&)
            {
                CMOOSMsg moos_msg(MOOS_NOTIFY, var, str);
                moos_msg.SetSourceAux(
                    pb_name + ":" +
                    goby::moos::protobuf::TranslatorEntry::ParserSerializerTechnique_Name(
                        technique));
                return moos_msg;
            }
        }
    }

  private:
    void initialize(double lat_origin = std::numeric_limits<double>::quiet_NaN(),
                    double lon_origin = std::numeric_limits<double>::quiet_NaN(),
                    const std::string& modem_id_lookup_path = "");

    void alg_lat2utm_y(moos::transitional::DCCLMessageVal& mv,
                       const std::vector<moos::transitional::DCCLMessageVal>& ref_vals);

    void alg_lon2utm_x(moos::transitional::DCCLMessageVal& mv,
                       const std::vector<moos::transitional::DCCLMessageVal>& ref_vals);

    void alg_utm_x2lon(moos::transitional::DCCLMessageVal& mv,
                       const std::vector<moos::transitional::DCCLMessageVal>& ref_vals);

    void alg_utm_y2lat(moos::transitional::DCCLMessageVal& mv,
                       const std::vector<moos::transitional::DCCLMessageVal>& ref_vals);

    void alg_modem_id2name(moos::transitional::DCCLMessageVal& in);
    void alg_modem_id2type(moos::transitional::DCCLMessageVal& in);
    void alg_name2modem_id(moos::transitional::DCCLMessageVal& in);

  private:
    std::map<std::string, goby::moos::protobuf::TranslatorEntry> dictionary_;
    CMOOSGeodesy geodesy_;
    goby::moos::ModemIdConvert modem_lookup_;
};

inline std::ostream& operator<<(std::ostream& os, const MOOSTranslator& tl)
{
    os << "= Begin MOOSTranslator =\n";

    int i = 0;
    for (const auto& it : tl.dictionary())
    {
        os << "== Begin Entry " << i << " ==\n"
           << it.second.DebugString() << "== End Entry " << i << " ==\n";

        ++i;
    }

    os << "= End MOOSTranslator =";
    return os;
}

namespace protobuf
{
inline bool operator<(const protobuf::TranslatorEntry& a, const protobuf::TranslatorEntry& b)
{
    return a.protobuf_name() < b.protobuf_name();
}
} // namespace protobuf

} // namespace moos
} // namespace goby

inline std::multimap<std::string, CMOOSMsg>
goby::moos::MOOSTranslator::protobuf_to_moos(const google::protobuf::Message& protobuf_msg)
{
    std::map<std::string, goby::moos::protobuf::TranslatorEntry>::const_iterator it =
        dictionary_.find(protobuf_msg.GetDescriptor()->full_name());

    const std::string& pb_name = protobuf_msg.GetDescriptor()->full_name();

    if (it == dictionary_.end())
        throw(std::runtime_error("No TranslatorEntry for Protobuf type: " + pb_name));

    const goby::moos::protobuf::TranslatorEntry& entry = it->second;

    std::multimap<std::string, CMOOSMsg> moos_msgs;

    bool is_binary = false;

    for (int i = 0, n = entry.publish_size(); i < n; ++i)
    {
        std::string return_string;
        std::string moos_var = entry.publish(i).moos_var();

        switch (entry.publish(i).technique())
        {
            case protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_TEXT_FORMAT:
                goby::moos::MOOSTranslation<
                    protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_TEXT_FORMAT>::
                    serialize(&return_string, protobuf_msg);
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_TEXT_FORMAT:
                goby::moos::MOOSTranslation<
                    protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_TEXT_FORMAT>::
                    serialize(&return_string, protobuf_msg);
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_HEX:
                goby::moos::MOOSTranslation<
                    protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_HEX>::
                    serialize(&return_string, protobuf_msg);
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_HEX:
                goby::moos::MOOSTranslation<
                    protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_HEX>::
                    serialize(&return_string, protobuf_msg);
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_ENCODED:
                goby::moos::MOOSTranslation<
                    protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_ENCODED>::
                    serialize(&return_string, protobuf_msg);
                is_binary = true;
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_ENCODED:
                goby::moos::MOOSTranslation<
                    protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_ENCODED>::
                    serialize(&return_string, protobuf_msg);
                is_binary = true;
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_COMMA_SEPARATED_KEY_EQUALS_VALUE_PAIRS:
                goby::moos::MOOSTranslation<
                    protobuf::TranslatorEntry::TECHNIQUE_COMMA_SEPARATED_KEY_EQUALS_VALUE_PAIRS>::
                    serialize(&return_string, protobuf_msg, entry.publish(i).algorithm(),
                              entry.use_short_enum());
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_FORMAT:
                // process moos_variable too (can be a format string itself!)
                goby::moos::MOOSTranslation<protobuf::TranslatorEntry::TECHNIQUE_FORMAT>::serialize(
                    &moos_var, protobuf_msg, entry.publish(i).algorithm(),
                    entry.publish(i).moos_var(), entry.publish(i).repeated_delimiter(),
                    entry.use_short_enum());
                // now do the format values
                goby::moos::MOOSTranslation<protobuf::TranslatorEntry::TECHNIQUE_FORMAT>::serialize(
                    &return_string, protobuf_msg, entry.publish(i).algorithm(),
                    entry.publish(i).format(), entry.publish(i).repeated_delimiter(),
                    entry.use_short_enum());
                break;
        }

        moos_msgs.insert(
            std::make_pair(moos_var, make_moos_msg(moos_var, return_string, is_binary,
                                                   entry.publish(i).technique(), pb_name)));
    }

    return moos_msgs;
}

inline std::multimap<std::string, CMOOSMsg>
goby::moos::MOOSTranslator::protobuf_to_inverse_moos(const google::protobuf::Message& protobuf_msg)
{
    std::map<std::string, goby::moos::protobuf::TranslatorEntry>::const_iterator it =
        dictionary_.find(protobuf_msg.GetDescriptor()->full_name());

    const std::string& pb_name = protobuf_msg.GetDescriptor()->full_name();

    if (it == dictionary_.end())
        throw(std::runtime_error("No TranslatorEntry for Protobuf type: " + pb_name));

    const goby::moos::protobuf::TranslatorEntry& entry = it->second;

    std::multimap<std::string, CMOOSMsg> moos_msgs;

    bool is_binary = false;

    for (int i = 0, n = entry.create_size(); i < n; ++i)
    {
        std::string return_string;
        std::string moos_var = entry.create(i).moos_var();

        switch (entry.create(i).technique())
        {
            case protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_TEXT_FORMAT:
                goby::moos::MOOSTranslation<
                    goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_TEXT_FORMAT>::
                    serialize(&return_string, protobuf_msg);
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_TEXT_FORMAT:
                goby::moos::MOOSTranslation<
                    goby::moos::protobuf::TranslatorEntry::
                        TECHNIQUE_PREFIXED_PROTOBUF_TEXT_FORMAT>::serialize(&return_string,
                                                                            protobuf_msg);
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_HEX:
                goby::moos::MOOSTranslation<
                    goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_HEX>::
                    serialize(&return_string, protobuf_msg);
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_HEX:
                goby::moos::MOOSTranslation<
                    goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_HEX>::
                    serialize(&return_string, protobuf_msg);
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_ENCODED:
                goby::moos::MOOSTranslation<
                    goby::moos::protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_ENCODED>::
                    serialize(&return_string, protobuf_msg);
                is_binary = true;
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_ENCODED:
                goby::moos::MOOSTranslation<
                    goby::moos::protobuf::TranslatorEntry::
                        TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_ENCODED>::serialize(&return_string,
                                                                               protobuf_msg);
                is_binary = true;
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_COMMA_SEPARATED_KEY_EQUALS_VALUE_PAIRS:
            {
                // workaround for bug in protobuf 2.3.0
                google::protobuf::RepeatedPtrField<
                    protobuf::TranslatorEntry::PublishSerializer::Algorithm>
                    empty_algorithms;

                goby::moos::MOOSTranslation<
                    protobuf::TranslatorEntry::TECHNIQUE_COMMA_SEPARATED_KEY_EQUALS_VALUE_PAIRS>::
                    serialize(&return_string, protobuf_msg, empty_algorithms,
                              entry.use_short_enum());
            }
            break;

            case protobuf::TranslatorEntry::TECHNIQUE_FORMAT:
            {
                google::protobuf::RepeatedPtrField<
                    protobuf::TranslatorEntry::PublishSerializer::Algorithm>
                    empty_algorithms;

                goby::moos::MOOSTranslation<protobuf::TranslatorEntry::TECHNIQUE_FORMAT>::serialize(
                    &return_string, protobuf_msg, empty_algorithms, entry.create(i).format(),
                    entry.create(i).repeated_delimiter(), entry.use_short_enum());
            }
            break;
        }

        moos_msgs.insert(
            std::make_pair(moos_var, make_moos_msg(moos_var, return_string, is_binary,
                                                   entry.create(i).technique(), pb_name)));
    }

    if (entry.trigger().type() == protobuf::TranslatorEntry::Trigger::TRIGGER_PUBLISH)
    {
        if (moos_msgs.count(entry.trigger().moos_var()))
        {
            // fake the trigger last so that all other inputs get read in first
            typedef std::multimap<std::string, CMOOSMsg>::iterator It;
            std::pair<It, It> p = moos_msgs.equal_range(entry.trigger().moos_var());
            for (It it = p.first; it != p.second; ++it) it->second.m_dfTime = MOOSTime();
        }
        else
        {
            // add a trigger
            moos_msgs.insert(std::make_pair(entry.trigger().moos_var(),
                                            CMOOSMsg(MOOS_NOTIFY, entry.trigger().moos_var(), "")));
        }
    }

    return moos_msgs;
}

template <typename GoogleProtobufMessagePointer, class StringCMOOSMsgMap>
GoogleProtobufMessagePointer
goby::moos::MOOSTranslator::moos_to_protobuf(const StringCMOOSMsgMap& moos_variables,
                                             const std::string& protobuf_name)
{
    std::map<std::string, goby::moos::protobuf::TranslatorEntry>::const_iterator it =
        dictionary_.find(protobuf_name);

    if (it == dictionary_.end())
        throw(std::runtime_error("No TranslatorEntry for Protobuf type: " + protobuf_name));

    const goby::moos::protobuf::TranslatorEntry& entry = it->second;

    GoogleProtobufMessagePointer msg;

    {
        // dccl::DynamicProtobufManager appears not to be thread safe
        const std::lock_guard<std::mutex> lock(goby::moos::dynamic_parse_mutex);
        msg = dccl::DynamicProtobufManager::new_protobuf_message<GoogleProtobufMessagePointer>(
            protobuf_name);
    }

    if (!msg)
        throw(std::runtime_error("Unknown Protobuf type: " + protobuf_name +
                                 "; be sure it is compiled in or directly loaded into the "
                                 "dccl::DynamicProtobufManager."));

    for (int i = 0, n = entry.create_size(); i < n; ++i)
    {
        std::multimap<std::string, CMOOSMsg>::const_iterator it =
            moos_variables.find(entry.create(i).moos_var());
        std::string source_string =
            (it == moos_variables.end())
                ? ""
                : (it->second.IsString() ? it->second.GetString()
                                         : goby::util::as<std::string>(it->second.GetDouble()));

        switch (entry.create(i).technique())
        {
            case protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_TEXT_FORMAT:
                goby::moos::MOOSTranslation<
                    protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_TEXT_FORMAT>::parse(source_string,
                                                                                      &*msg);
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_TEXT_FORMAT:
                goby::moos::MOOSTranslation<
                    protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_TEXT_FORMAT>::
                    parse(source_string, &*msg);
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_HEX:
                goby::moos::MOOSTranslation<
                    protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_HEX>::parse(source_string,
                                                                                     &*msg);
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_HEX:
                goby::moos::MOOSTranslation<
                    protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_HEX>::
                    parse(source_string, &*msg);
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_ENCODED:
                goby::moos::MOOSTranslation<
                    protobuf::TranslatorEntry::TECHNIQUE_PROTOBUF_NATIVE_ENCODED>::
                    parse(source_string, &*msg);
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_ENCODED:
                goby::moos::MOOSTranslation<
                    protobuf::TranslatorEntry::TECHNIQUE_PREFIXED_PROTOBUF_NATIVE_ENCODED>::
                    parse(source_string, &*msg);
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_COMMA_SEPARATED_KEY_EQUALS_VALUE_PAIRS:
                goby::moos::MOOSTranslation<
                    protobuf::TranslatorEntry::TECHNIQUE_COMMA_SEPARATED_KEY_EQUALS_VALUE_PAIRS>::
                    parse(source_string, &*msg, entry.create(i).algorithm(),
                          entry.use_short_enum());
                break;

            case protobuf::TranslatorEntry::TECHNIQUE_FORMAT:
                goby::moos::MOOSTranslation<protobuf::TranslatorEntry::TECHNIQUE_FORMAT>::parse(
                    source_string, &*msg, entry.create(i).format(),
                    entry.create(i).repeated_delimiter(), entry.create(i).algorithm(),
                    entry.use_short_enum());
                break;
        }
    }

    return msg;
}

#endif
