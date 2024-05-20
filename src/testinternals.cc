/*
 Copyright (C) 2018-2024 Fredrik Öhrström (gpl-3.0-or-later)

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include"address.h"
#include"aes.h"
#include"aescmac.h"
#include"cmdline.h"
#include"config.h"
#include"formula_implementation.h"
#include"meters.h"
#include"printer.h"
#include"serial.h"
#include"translatebits.h"
#include"util.h"
#include"wmbus.h"
#include"dvparser.h"

#include<string.h>
#include<set>

using namespace std;

// This is test specific verbose.
bool verbose_ = false;

#define LIST_OF_TESTS \
    X(addresses) \
    X(dynamic_loading)                        \
    X(crc)            \
    X(dvparser)       \
    X(devices)        \
    X(linkmodes)      \
    X(ids)            \
    X(kdf)            \
    X(periods)        \
    X(device_parsing) \
    X(meters)         \
    X(months)         \
    X(aes)            \
    X(sbc)            \
    X(hex)            \
    X(translate)                                \
    X(slip)                                     \
    X(dvs)                                      \
    X(ascii_detection)                          \
    X(status_join)                              \
    X(status_sort)                              \
    X(field_matcher)                            \
    X(units_extraction)                         \
    X(si_units_siexp)                           \
    X(si_units_basic)                           \
    X(si_units_conversion)                      \
    X(formulas_building_consts)                 \
    X(formulas_building_meters)                 \
    X(formulas_datetimes)                       \
    X(formulas_parsing_1)                       \
    X(formulas_parsing_2)                       \
    X(formulas_multiply_constants)              \
    X(formulas_divide_constants)                \
    X(formulas_sqrt_constants)                  \
    X(formulas_errors)                          \
    X(formulas_dventries)                       \
    X(formulas_stringinterpolation)             \

#define X(t) void test_##t();
LIST_OF_TESTS
#undef X

// Test if we should run this test based on the command line pattern.
bool test(const char *test_name, const char *pattern)
{
    if (pattern == NULL)
    {
        if (verbose_) printf("Test %s\n", test_name);
        return true;
    }
    bool ok = strstr(test_name, pattern) != NULL;
    if (ok) info("Test %s\n", test_name);
    return ok;
}

int main(int argc, char **argv)
{
    const char *pattern = NULL;

    int i = 1;
    while (i < argc)
    {
        if (!strcmp(argv[i], "--verbose"))
        {
            verbose_ = true;
        }
        else
        if (!strcmp(argv[i], "--debug"))
        {
            verbose_ = true;
            debugEnabled(true);
        }
        else
        if (!strcmp(argv[i], "--trace"))
        {
            verbose_ = true;
            debugEnabled(true);
            traceEnabled(true);
        }
        else
        {
            pattern = argv[i];
        }
        i++;
    }
    onExit([](){});

#define X(x) if (test(#x, pattern)) test_##x();
LIST_OF_TESTS
#undef X

    return 0;
}

void test_crc()
{
    unsigned char data[4];
    data[0] = 0x01;
    data[1] = 0xfd;
    data[2] = 0x1f;
    data[3] = 0x01;

    uint16_t crc = crc16_EN13757(data, 4);
    if (crc != 0xcc22) {
        printf("ERROR! %4x should be cc22\n", crc);
    }
    data[3] = 0x00;

    crc = crc16_EN13757(data, 4);
    if (crc != 0xf147) {
        printf("ERROR! %4x should be f147\n", crc);
    }

    uchar block[10];
    block[0]=0xEE;
    block[1]=0x44;
    block[2]=0x9A;
    block[3]=0xCE;
    block[4]=0x01;
    block[5]=0x00;
    block[6]=0x00;
    block[7]=0x80;
    block[8]=0x23;
    block[9]=0x07;

    crc = crc16_EN13757(block, 10);

    if (crc != 0xaabc) {
        printf("ERROR! %4x should be aabc\n", crc);
    }

    block[0]='1';
    block[1]='2';
    block[2]='3';
    block[3]='4';
    block[4]='5';
    block[5]='6';
    block[6]='7';
    block[7]='8';
    block[8]='9';

    crc = crc16_EN13757(block, 9);

    if (crc != 0xc2b7) {
        printf("ERROR! %4x should be c2b7\n", crc);
    }
}

bool tst_parse(const char *data, std::map<std::string,std::pair<int,DVEntry>> *dv_entries, int testnr)
{
    debug("\n\nTest nr %d......\n\n", testnr);
    bool b;
    Telegram t;
    vector<uchar> databytes;
    hex2bin(data, &databytes);
    vector<uchar>::iterator i = databytes.begin();

    b = parseDV(&t, databytes, i, databytes.size(), dv_entries);

    return b;
}

void tst_double(map<string,pair<int,DVEntry>> &values, const char *key, double v, int testnr)
{
    int offset;
    double value;
    bool b =  extractDVdouble(&values,
                              key,
                              &offset,
                              &value);

    if (!b || value != v) {
        fprintf(stderr, "Error in dvparser testnr %d: got %lf but expected value %lf for key %s\n", testnr, value, v, key);
    }
}

void tst_string(map<string,pair<int,DVEntry>> &values, const char *key, const char *v, int testnr)
{
    int offset;
    string value;
    bool b = extractDVHexString(&values,
                                key,
                                &offset,
                                &value);
    if (!b || value != v) {
        fprintf(stderr, "Error in dvparser testnr %d: got \"%s\" but expected value \"%s\" for key %s\n", testnr, value.c_str(), v, key);
    }
}

void tst_date(map<string,pair<int,DVEntry>> &values, const char *key, string date_expected, int testnr)
{
    int offset;
    struct tm value;
    bool b = extractDVdate(&values,
                           key,
                           &offset,
                           &value);

    char buf[256];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &value);
    string date_got = buf;
    if (!b || date_got != date_expected) {
        fprintf(stderr, "Error in dvparser testnr %d:\ngot >%s< but expected >%s< for key %s\n\n", testnr, date_got.c_str(), date_expected.c_str(), key);
    }
}

void test_dvparser()
{
    map<string,pair<int,DVEntry>> dv_entries;

    int testnr = 1;
    tst_parse("2F 2F 0B 13 56 34 12 8B 82 00 93 3E 67 45 23 0D FD 10 0A 30 31 32 33 34 35 36 37 38 39 0F 88 2F", &dv_entries, testnr);
    tst_double(dv_entries, "0B13", 123.456, testnr);
    tst_double(dv_entries, "8B8200933E", 234.567, testnr);
    tst_string(dv_entries, "0DFD10", "30313233343536373839", testnr);

    testnr++;
    dv_entries.clear();
    tst_parse("82046C 5f1C", &dv_entries, testnr);
    tst_date(dv_entries, "82046C", "2010-12-31 00:00:00", testnr); // 2010-dec-31

    testnr++;
    dv_entries.clear();
    tst_parse("0C1348550000426CE1F14C130000000082046C21298C0413330000008D04931E3A3CFE3300000033000000330000003300000033000000330000003300000033000000330000003300000033000000330000004300000034180000046D0D0B5C2B03FD6C5E150082206C5C290BFD0F0200018C4079678885238310FD3100000082106C01018110FD610002FD66020002FD170000", &dv_entries, testnr);
    tst_double(dv_entries, "0C13", 5.548, testnr);
    tst_date(dv_entries, "426C", "2127-01-01 00:00:00", testnr); // 2127-jan-1
    tst_date(dv_entries, "82106C", "2000-01-01 00:00:00", testnr); // 2000-jan-1

    testnr++;
    dv_entries.clear();
    tst_parse("426C FE04", &dv_entries, testnr);
    tst_date(dv_entries, "426C", "2007-04-30 00:00:00", testnr); // 2010-dec-31
}

void test_devices()
{
    shared_ptr<SerialCommunicationManager> manager = createSerialCommunicationManager(0, false);

    shared_ptr<SerialDevice> serial1 = manager->createSerialDeviceSimulator();

    /*
    shared_ptr<BusDevice> wmbus_im871a = openIM871A("", manager, serial1);
    manager->stop();*/
}

void test_linkmodes()
{
    /*
    LinkModeCalculationResult lmcr;
    auto manager = createSerialCommunicationManager(0, false);

    auto serial1 = manager->createSerialDeviceSimulator();
    auto serial2 = manager->createSerialDeviceSimulator();
    auto serial3 = manager->createSerialDeviceSimulator();
    auto serial4 = manager->createSerialDeviceSimulator();
    auto serial5 = manager->createSerialDeviceSimulator();

    vector<string> no_meter_shells, no_meter_jsons;
    Detected de;
    SpecifiedDevice sd;
    shared_ptr<BusDevice> wmbus_im871a = openIM871A(de, manager, serial1);
    shared_ptr<BusDevice> wmbus_amb8465 = openAMB8465(de, manager, serial2);
    shared_ptr<BusDevice> wmbus_rtlwmbus = openRTLWMBUS(de, "", false, manager, serial3);
    shared_ptr<BusDevice> wmbus_rawtty = openRawTTY(de, manager, serial4);
    shared_ptr<BusDevice> wmbus_amb3665 = openAMB3665(de, manager, serial5);

    Configuration nometers_config;
    // Check that if no meters are supplied then you must set a link mode.
    lmcr = calculateLinkModes(&nometers_config, wmbus_im871a.get());
    if (lmcr.type != LinkModeCalculationResultType::NoMetersMustSupplyModes)
    {
        printf("ERROR! Expected failure due to automatic deduction! Got instead:\n%s\n", lmcr.msg.c_str());
    }
    debug("test0 OK\n\n");

    nometers_config.default_device_linkmodes.addLinkMode(LinkMode::T1);
    lmcr = calculateLinkModes(&nometers_config, wmbus_im871a.get());
    if (lmcr.type != LinkModeCalculationResultType::Success)
    {
        printf("ERROR! Expected succcess! Got instead:\n%s\n", lmcr.msg.c_str());
    }
    debug("test0.0 OK\n\n");

    Configuration apator_config;
    string apator162 = "apator162";
    vector<string> ids = { "12345678" };
    apator_config.meters.push_back(MeterInfo("", // bus
                                             "m1", // name
                                             MeterDriver::APATOR162, // driver/type
                                             "", // extras
                                             ids, // ids
                                             "", // Key
                                             toMeterLinkModeSet(apator162), // link mode set
                                             0, // baud
                                             no_meter_shells, // shells
                                             no_meter_jsons)); // jsons

    // Check that if no explicit link modes are provided to apator162, then
    // automatic deduction will fail, since apator162 can be configured to transmit
    // either C1 or T1 telegrams.
    lmcr = calculateLinkModes(&apator_config, wmbus_im871a.get());
    if (lmcr.type != LinkModeCalculationResultType::AutomaticDeductionFailed)
    {
        printf("ERROR! Expected failure due to automatic deduction! Got instead:\n%s\n", lmcr.msg.c_str());
    }
    debug("test1 OK\n\n");

    // Check that if we supply the link mode T1 when using an apator162, then
    // automatic deduction will succeeed.
    apator_config.default_device_linkmodes = LinkModeSet();
    apator_config.default_device_linkmodes.addLinkMode(LinkMode::T1);
    apator_config.default_device_linkmodes.addLinkMode(LinkMode::C1);
    lmcr = calculateLinkModes(&apator_config, wmbus_im871a.get());
    if (lmcr.type != LinkModeCalculationResultType::DongleCannotListenTo)
    {
        printf("ERROR! Expected dongle cannot listen to! Got instead:\n%s\n", lmcr.msg.c_str());
    }
    debug("test2 OK\n\n");

    lmcr = calculateLinkModes(&apator_config, wmbus_rtlwmbus.get());
    if (lmcr.type != LinkModeCalculationResultType::Success)
    {
        printf("ERROR! Expected success! Got instead:\n%s\n", lmcr.msg.c_str());
    }
    debug("test3 OK\n\n");

    Configuration multical21_and_supercom587_config;
    string multical21 = "multical21";
    string supercom587 = "supercom587";

    multical21_and_supercom587_config.meters.push_back(MeterInfo("", "m1", MeterDriver::MULTICAL21, "", ids, "",
                                                                 toMeterLinkModeSet(multical21),
                                                                 0,
                                                                 no_meter_shells,
                                                                 no_meter_jsons));
    multical21_and_supercom587_config.meters.push_back(MeterInfo("", "m2", MeterDriver::UNKNOWN, "supercom587", ids, "",
                                                                 toMeterLinkModeSet(supercom587),
                                                                 0,
                                                                 no_meter_shells,
                                                                 no_meter_jsons));

    // Check that meters that transmit on two different link modes cannot be listened to
    // at the same time using im871a.
    lmcr = calculateLinkModes(&multical21_and_supercom587_config, wmbus_im871a.get());
    if (lmcr.type != LinkModeCalculationResultType::AutomaticDeductionFailed)
    {
        printf("ERROR! Expected failure due to automatic deduction! Got instead:\n%s\n", lmcr.msg.c_str());
    }
    debug("test4 OK\n\n");

    // Explicitly set T1
    multical21_and_supercom587_config.default_device_linkmodes = LinkModeSet();
    multical21_and_supercom587_config.default_device_linkmodes.addLinkMode(LinkMode::T1);
    lmcr = calculateLinkModes(&multical21_and_supercom587_config, wmbus_im871a.get());
    if (lmcr.type != LinkModeCalculationResultType::MightMissTelegrams)
    {
        printf("ERROR! Expected might miss telegrams! Got instead:\n%s\n", lmcr.msg.c_str());
    }
    debug("test5 OK\n\n");

    // Explicitly set N1a, but the meters transmit on C1 and T1.
    multical21_and_supercom587_config.default_device_linkmodes = LinkModeSet();
    multical21_and_supercom587_config.default_device_linkmodes.addLinkMode(LinkMode::N1a);
    lmcr = calculateLinkModes(&multical21_and_supercom587_config, wmbus_im871a.get());
    if (lmcr.type != LinkModeCalculationResultType::MightMissTelegrams)
    {
        printf("ERROR! Expected no meter can be heard! Got instead:\n%s\n", lmcr.msg.c_str());
    }
    debug("test6 OK\n\n");

    // Explicitly set N1a, but it is an amber dongle.
    multical21_and_supercom587_config.default_device_linkmodes = LinkModeSet();
    multical21_and_supercom587_config.default_device_linkmodes.addLinkMode(LinkMode::N1a);
    lmcr = calculateLinkModes(&multical21_and_supercom587_config, wmbus_amb8465.get());
    if (lmcr.type != LinkModeCalculationResultType::DongleCannotListenTo)
    {
        printf("ERROR! Expected dongle cannot listen to! Got instead:\n%s\n", lmcr.msg.c_str());
    }
    debug("test7 OK\n\n");

    manager->stop();
    */
}

void test_valid_match_expression(string s, bool expected)
{
    bool b = isValidSequenceOfAddressExpressions(s);
    if (b == expected) return;
    if (expected == true)
    {
        printf("ERROR! Expected \"%s\" to be valid! But it was not!\n", s.c_str());
    }
    else
    {
        printf("ERROR! Expected \"%s\" to be invalid! But it was reported as valid!\n", s.c_str());
    }
}

void test_does_id_match_expression(string id, string mes, bool expected, bool expected_uw)
{
    Address a;
    a.id = id;
    vector<Address> as;
    as.push_back(a);
    vector<AddressExpression> expressions = splitAddressExpressions(mes);
    bool uw = false;
    bool b = doesTelegramMatchExpressions(as, expressions, &uw);
    if (b != expected)
    {
        if (expected == true)
        {
            printf("ERROR! Expected \"%s\" to match \"%s\" ! But it did not!\n", id.c_str(), mes.c_str());
        }
        else
        {
            printf("ERROR! Expected \"%s\" to NOT match \"%s\" ! But it did!\n", id.c_str(), mes.c_str());
        }
    }
    if (expected_uw != uw)
    {
        printf("ERROR! Matching \"%s\" \"%s\" and expecte used_wildcard %d but got %d!\n",
               id.c_str(), mes.c_str(), expected_uw, uw);
    }
}

void test_ids()
{
    test_valid_match_expression("12345678", true);
    test_valid_match_expression("*", true);
    test_valid_match_expression("!12345678", true);
    test_valid_match_expression("12345*", true);
    test_valid_match_expression("!123456*", true);

    test_valid_match_expression("1234567", false);
    test_valid_match_expression("", false);
    test_valid_match_expression("z1234567", false);
    test_valid_match_expression("123456789", false);
    test_valid_match_expression("!!12345678", false);
    test_valid_match_expression("12345678*", false);
    test_valid_match_expression("**", false);
    test_valid_match_expression("123**", false);

    test_valid_match_expression("2222*,!22224444", true);

    test_does_id_match_expression("12345678", "12345678", true, false);
    test_does_id_match_expression("12345678", "*", true, true);
    test_does_id_match_expression("12345678", "2*", false, false);

    test_does_id_match_expression("12345678", "*,!2*", true, true);

    test_does_id_match_expression("22222222", "22*,!22222222", false, false);
    test_does_id_match_expression("22222223", "22*,!22222222", true, true);
    test_does_id_match_expression("22222223", "*,!22*", false, false);
    test_does_id_match_expression("12333333", "123*,!1234*,!1235*,!1236*", true, true);
    test_does_id_match_expression("12366666", "123*,!1234*,!1235*,!1236*", false, false);
    test_does_id_match_expression("11223344", "22*,33*,44*,55*", false, false);
    test_does_id_match_expression("55223344", "22*,33*,44*,55*", true, true);

    test_does_id_match_expression("78563413", "78563412,78563413", true, false);
    test_does_id_match_expression("78563413", "*,!00156327,!00048713", true, true);
}

void tst_address(string s, bool valid,
                 string id, bool has_wildcard,
                 string mfct,
                 uchar version,
                 uchar type,
                 bool mbus_primary,
                 bool filter_out)
{
    AddressExpression a;
    bool ok = a.parse(s);

    if (ok != valid)
    {
        printf("Expected parse of address \"%s\" to return %s, but returned %s\n",
               s.c_str(),
               valid?"valid":"bad", ok?"valid":"bad");
    }
    if (ok)
    {
        string smfct = manufacturerFlag(a.mfct);
        if (id != a.id ||
            has_wildcard != a.has_wildcard ||
            mfct != smfct ||
            version != a.version ||
            type != a.type ||
            mbus_primary != a.mbus_primary ||
            filter_out != a.filter_out)
        {
            printf("Expected parse of address \"%s\" to return\n"
                   "(id=%s haswild=%d mfct=%s version=%02x type=%02x mbus=%d negt=%d)\n"
                   "but got\n"
                   "(id=%s haswild=%d mfct=%s version=%02x type=%02x mbus=%d negt=%d)\n",
                   s.c_str(),
                   id.c_str(),
                   has_wildcard,
                   mfct.c_str(),
                   version,
                   type,
                   mbus_primary,
                   filter_out,
                   a.id.c_str(),
                   a.has_wildcard,
                   smfct.c_str(),
                   a.version,
                   a.type,
                   a.mbus_primary,
                   a.filter_out);
        }
    }
}

void tst_address_match(string expr, string id, uint16_t m, uchar v, uchar t, bool match, bool filter_out)
{
    AddressExpression e;
    bool ok = e.parse(expr);
    assert(ok);

    bool test = e.match(id, m, v, t);

    if (test != match)
    {
        printf("Expected address %s %04x %02x %02x to %smatch expression %s\n",
               id.c_str(),
               m, v, t,
               match?"":"not ",
               expr.c_str());
    }
    if (match && e.filter_out != filter_out)
    {
        printf("Expected %s from match expression %s\n",
               filter_out?"FILTERED OUT":"NOT filtered",
               expr.c_str());
    }
}

void tst_telegram_match(string addresses, string expressions, bool match, bool uw)
{
    vector<AddressExpression> exprs = splitAddressExpressions(expressions);
    vector<AddressExpression> as = splitAddressExpressions(addresses);
    vector<Address> addrs;

    for (auto &ad : as)
    {
        Address a;
        a.id = ad.id;
        a.mfct = ad.mfct;
        a.version = ad.version;
        a.type = ad.type;

        addrs.push_back(a);
    }

    bool used_wildcard = false;
    bool m = doesTelegramMatchExpressions(addrs, exprs, &used_wildcard);

    if (m != match)
    {
        printf("Expected addresses %s to %smatch expressions %s\n",
               addresses.c_str(),
               match?"":"NOT ",
               expressions.c_str());
    }
    if (uw != used_wildcard)
    {
        printf("Expected addresses %s from match expression %s %susing wildcard\n",
               addresses.c_str(),
               expressions.c_str(),
               uw?"":"NOT ");
    }
}

void test_addresses()
{
    tst_address("12345678",
                true,
                "12345678", // id
                false, // has wildcard
                "___", // mfct
                0xff, // type
                0xff,  // version
                false, // mbus primary found
                false // negate test
        );
    tst_address("123k45678", false, "", false, "", 0xff, 0xff, false, false);
    tst_address("1234", false, "", false, "", 0xff, 0xff, false, false);
    tst_address("p0", true, "p0", false, "___", 0xff, 0xff, true, false);
    tst_address("p250", true, "p250", false, "___", 0xff, 0xff, true, false);
    tst_address("p251", false, "", false, "", 0xff, 0xff, false, false);
    tst_address("p0.M=PII.V=01.T=1b", true, "p0", false, "PII", 0x01, 0x1b, true, false);
    tst_address("p123.V=11.M=FOO.T=ff", true, "p123", false, "FOO", 0x11, 0xff, true, false);
    tst_address("p123.M=FOO", true, "p123", false, "FOO", 0xff, 0xff, true, false);
    tst_address("p123.M=FOO.V=33", true, "p123", false, "FOO", 0x33, 0xff, true, false);
    tst_address("p123.T=33", true, "p123", false, "___", 0xff, 0x33, true, false);
    tst_address("p1.V=33", true, "p1", false, "___", 0x33, 0xff, true, false);
    tst_address("p16.M=BAR", true, "p16", false, "BAR", 0xff, 0xff, true, false);

    tst_address("12345678.M=ABB.V=66.T=16", true, "12345678",  false, "ABB", 0x66, 0x16, false, false);
    tst_address("!12345678.M=ABB.V=66.T=16", true, "12345678", false, "ABB", 0x66, 0x16, false, true);
    tst_address("!*.M=ABB", true, "*", true, "ABB", 0xff, 0xff, false, true);
    tst_address("!*.V=66.T=06", true, "*", true, "___", 0x66, 0x06, false, true);

    tst_address("12*", true, "12*", true, "___", 0xff, 0xff, false, false);
    tst_address("!1234567*", true, "1234567*", true, "___", 0xff, 0xff, false, true);

    tst_address_match("12345678", "12345678", 1, 1, 1, true, false);
    tst_address_match("12345678.M=ABB.V=77", "12345678", MANUFACTURER_ABB, 0x77, 88, true, false);
    tst_address_match("1*.V=77", "12345678", MANUFACTURER_ABB, 0x77, 1, true, false);
    tst_address_match("12345678.M=ABB.V=67.T=06", "12345678", MANUFACTURER_ABB, 0x67, 0x06, true, false);
    tst_address_match("12345678.M=ABB.V=67.T=06", "12345678", MANUFACTURER_ABB, 0x68, 0x06, false, false);
    tst_address_match("12345678.M=ABB.V=67.T=06", "12345678", MANUFACTURER_ABB, 0x67, 0x07, false, false);
    tst_address_match("12345678.M=ABB.V=67.T=06", "12345678", MANUFACTURER_ABB+1, 0x67, 0x06, false, false);
    tst_address_match("12345678.M=ABB.V=67.T=06", "12345677", MANUFACTURER_ABB, 0x67, 0x06, false, false);

    // Now verify filter out ! character. The filter out does notchange the test. It is still the same
    // test, but the match will be used as a filter out. Ie if the match triggers, then the telegram will be filtered out.
    tst_address_match("!12345678", "12345677", 1, 1, 1, false, false);
    tst_address_match("!*.M=ABB", "99999999", MANUFACTURER_ABB, 1, 1, true, true);
    tst_address_match("*.M=ABB", "99999999", MANUFACTURER_ABB, 1, 1, true, false);

    // Test that both id wildcard matches and the version.
    tst_address_match("9*.V=06", "99999999", MANUFACTURER_ABB, 0x06, 1, true, false);
    tst_address_match("9*.V=06", "89999999", MANUFACTURER_ABB, 0x06, 1, false, false);
    tst_address_match("9*.V=06", "99999999", MANUFACTURER_ABB, 0x07, 1, false, false);
    tst_address_match("9*.V=06", "89999999", MANUFACTURER_ABB, 0x07, 1, false, false);

    // Test the same, expect same answers but check that filtered out is set.
    tst_address_match("!9*.V=06", "99999999", MANUFACTURER_ABB, 0x06, 1, true, true);
    tst_address_match("!9*.V=06", "89999999", MANUFACTURER_ABB, 0x06, 1, false, true);
    tst_address_match("!9*.V=06", "99999999", MANUFACTURER_ABB, 0x07, 1, false, true);
    tst_address_match("!9*.V=06", "89999999", MANUFACTURER_ABB, 0x07, 1, false, true);

    tst_telegram_match("12345678", "12345678", true, false);
    tst_telegram_match("11111111,22222222", "12345678,22*", true, true);
    tst_telegram_match("11111111,22222222", "12345678,22222222", true, false);
    tst_telegram_match("11111111.M=KAM,22222222.M=PII", "11111111.M=KAM", true, false);
    tst_telegram_match("11111111.M=KAF", "11111111.M=KAM", false, false);

    tst_telegram_match("11111111.M=KAM.V=1b.T=16", "11111111.M=KAM", true, false);
    tst_telegram_match("11111111.M=KAM.V=1b.T=16", "11111111.M=KAF", false, false);
    tst_telegram_match("11111111.M=KAM.V=1b.T=16", "11111111", true, false);
    tst_telegram_match("11111111.M=KAM.V=1b.T=16", "11111111.M=KAM", true, false);
    tst_telegram_match("11111111.M=KAM.V=1b.T=16", "11111111.V=1b", true, false);
    tst_telegram_match("11111111.M=KAM.V=1b.T=16", "11111111.T=16", true, false);
    tst_telegram_match("11111111.M=KAM.V=1b.T=16", "11111111.M=KAM.T=16", true, false);
    tst_telegram_match("11111111.M=KAM.V=1b.T=16", "11111111.M=KAM.V=1b", true, false);
    tst_telegram_match("11111111.M=KAM.V=1b.T=16", "11111111.T=16.V=1b", true, false);

    tst_telegram_match("11111111.M=KAM.V=1b.T=16", "11111111.M=KAL", false, false);
    tst_telegram_match("11111111.M=KAM.V=1b.T=16", "11111111.V=1c", false, false);
    tst_telegram_match("11111111.M=KAM.V=1b.T=16", "11111111.T=17", false, false);
    tst_telegram_match("11111111.M=KAM.V=1b.T=16", "11111111.M=KAM.T=17", false, false);
    tst_telegram_match("11111111.M=KAM.V=1b.T=16", "11111111.M=KAL.V=1b", false, false);
    tst_telegram_match("11111111.M=KAM.V=1b.T=16", "11111111.T=17.V=1b", false, false);

    // Test * matches both 11111111 and 2222222 but the only the 111111 matches the filter out V=1b.
    // Verify that the filter out !1*.V=1b will override successfull match (with no filter out) * for 22222222.
    tst_telegram_match("11111111.M=KAM.V=1b.T=16,22222222.M=XXX.V=aa.T=99", "*,!1*.V=1b", false, true);
}

void eq(string a, string b, const char *tn)
{
    if (a != b)
    {
        printf("ERROR in test %s expected \"%s\" to be equal to \"%s\"\n", tn, a.c_str(), b.c_str());
    }
}

void eqn(int a, int b, const char *tn)
{
    if (a != b)
    {
        printf("ERROR in test %s expected %d to be equal to %d\n", tn, a, b);
    }
}

void test_kdf()
{
    vector<uchar> key;
    vector<uchar> input;
    vector<uchar> mac;

    hex2bin("2b7e151628aed2a6abf7158809cf4f3c", &key);
    mac.resize(16);

    AES_CMAC(safeButUnsafeVectorPtr(key),
             safeButUnsafeVectorPtr(input), 0,
             safeButUnsafeVectorPtr(mac));
    string s = bin2hex(mac);
    string ex = "BB1D6929E95937287FA37D129B756746";
    if (s != ex)
    {
        printf("ERROR in aes-cmac expected \"%s\" but got \"%s\"\n", ex.c_str(), s.c_str());
    }


    input.clear();
    hex2bin("6bc1bee22e409f96e93d7e117393172a", &input);
    AES_CMAC(safeButUnsafeVectorPtr(key),
             safeButUnsafeVectorPtr(input), 16,
             safeButUnsafeVectorPtr(mac));
    s = bin2hex(mac);
    ex = "070A16B46B4D4144F79BDD9DD04A287C";

    if (s != ex)
    {
        printf("ERROR in aes-cmac expected \"%s\" but got \"%s\"\n", ex.c_str(), s.c_str());
    }
}

void testp(time_t now, string period, bool expected)
{
    bool rc = isInsideTimePeriod(now, period);

    char buf[256];
    struct tm now_tm {};
    localtime_r(&now, &now_tm);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M %A", &now_tm);
    string nows = buf;

    if (expected == true && rc == false)
    {
        printf("ERROR in period test is \"%s\" in period \"%s\"? Expected true but got false!\n", nows.c_str(), period.c_str());
    }
    if (expected == false && rc == true)
    {
        printf("ERROR in period test is \"%s\" in period \"%s\"? Expected false but got true!\n", nows.c_str(), period.c_str());
    }
}

void test_periods()
{
    // 3600*24*7+3600 means 1970-01-08 01:00 Thursday in Greenwich.
    // However your local time is adjusted with your timezone.
    // Get your timezone offset tm_gmtoff into the value.
    time_t t = 3600*24*7+3600;
    struct tm value {};
    localtime_r(&t, &value);

    // if tm_gmtoff is zero, then we are in Greenwich!
    // if tm_gmtoff is 3600 then we are in Stockholm!

    t -= value.tm_gmtoff;

    // We have now adjusted the t so that it should be thursday at 1 am.
    // The following test should therefore work inependently on
    // where in the world this test is run.
    testp(t, "mon-sun(00-23)", true);
    testp(t, "mon(00-23)", false);
    testp(t, "thu-fri(01-01)", true);
    testp(t, "mon-wed(00-23),thu(02-23),fri-sun(00-23)", false);
    testp(t, "mon-wed(00-23),thu(01-23),fri-sun(00-23)", true);
    testp(t, "thu(00-00)", false);
    testp(t, "thu(01-01)", true);
}

void testd(string arg, bool xok, string xalias, string xfile, string xtype, string xid, string xextras,
           string xfq, string xbps, string xlm, string xcmd)
{
    SpecifiedDevice d;
    bool ok = d.parse(arg);
    if (ok != xok)
    {
        printf("ERROR in device parse test \"%s\" expected %s but got %s\n", arg.c_str(), xok?"OK":"FALSE", ok?"OK":"FALSE");
        return;
    }
    if (ok == false) return;

    if (d.bus_alias != xalias ||
        d.file != xfile ||
        toString(d.type) != xtype ||
        d.id != xid ||
        d.extras != xextras ||
        d.fq != xfq ||
        d.bps != xbps ||
        d.linkmodes.hr() != xlm ||
        d.command != xcmd)
    {
        printf("ERROR in bus device parsing parts \"%s\" - got\n"
               "alias: \"%s\", file: \"%s\", type: \"%s\", id: \"%s\", extras: \"%s\", fq: \"%s\", bps: \"%s\", lm: \"%s\", cmd: \"%s\"\n"
               "but expected:\n"
               "alias: \"%s\", file: \"%s\", type: \"%s\", id: \"%s\", extras: \"%s\", fq: \"%s\", bps: \"%s\", lm: \"%s\", cmd: \"%s\"\n",

               arg.c_str(),
               d.bus_alias.c_str(),
               d.file.c_str(),
               toString(d.type),
               d.id.c_str(),
               d.extras.c_str(),
               d.fq.c_str(),
               d.bps.c_str(),
               d.linkmodes.hr().c_str(),
               d.command.c_str(),

               xalias.c_str(),
               xfile.c_str(),
               xtype.c_str(),
               xid.c_str(),
               xextras.c_str(),
               xfq.c_str(),
               xbps.c_str(),
               xlm.c_str(),
               xcmd.c_str());

    }
}

void test_device_parsing()
{
    testd("Bus_4711=/dev/ttyUSB0:im871a[12345678]:9600:868.95M:c1,t1", true,
          "Bus_4711", // alias
          "/dev/ttyUSB0", // file
          "im871a", // type
          "12345678", // id
          "", // extras
          "868.95M", // fq
          "9600", // bps
          "t1,c1", // linkmodes
          ""); // command

    testd("/dev/ttyUSB0:im871a:c1", true,
          "", // alias
          "/dev/ttyUSB0", // file
          "im871a", // type
          "", // id
          "", // extras
          "", // fq
          "", // bps
          "c1", // linkmodes
          ""); // command

    testd("im871a[12345678]:c1", true,
          "", // alias
          "", // file
          "im871a", // type
          "12345678", // id
          "", // extras
          "", // fq
          "", // bps
          "c1", // linkmodes
          ""); // command

    testd("im871a(track=7,pi=3.14):c1", true,
          "", // alias
          "", // file
          "im871a", // type
          "", // id
          "track=7,pi=3.14", // extras
          "", // fq
          "", // bps
          "c1", // linkmodes
          ""); // command

    testd("rtlwmbus:c1,t1:CMD(gurka)", true,
          "", // alias
          "", // file
          "rtlwmbus", // type
          "", // id
          "", // extras
          "", // fq
          "", // bps
          "t1,c1", // linkmodes
          "gurka"); // command

    testd("rtlwmbus[plast]:c1,t1", true,
          "", // alias
          "", // file
          "rtlwmbus", // type
          "plast", // id
          "", // extras
          "", // fq
          "", // bps
          "t1,c1", // linkmodes
          ""); // command

    testd("ANTENNA1=rtlwmbus[plast](ppm=5):c1,t1", true,
          "ANTENNA1", // alias
          "", // file
          "rtlwmbus", // type
          "plast", // id
          "ppm=5", // extras
          "", // fq
          "", // bps
          "t1,c1", // linkmodes
          ""); // command

    testd("stdin:rtlwmbus", true,
          "", // alias
          "stdin", // file
          "rtlwmbus", // type
          "", // id
          "", // extras
          "", // fq
          "", // bps
          "none", // linkmodes
          ""); // command

    testd("/dev/ttyUSB0:rawtty:9600", true,
          "", // alias
          "/dev/ttyUSB0", // file
          "rawtty", // type
          "", // id
          "", // extras
          "", // fq
          "9600", // bps
          "none", // linkmodes
          ""); // command

    // testinternals must be run from a location where
    // there is a Makefile.
    testd("Makefile:simulation", true,
          "", // alias
          "Makefile", // file
          "simulation", // type
          "", // id
          "", // extras
          "", // fq
          "", // bps
          "none", // linkmodes
          ""); // command

    testd("auto:c1,t1", true,
          "", // alias
          "", // file
          "auto", // type
          "", // id
          "", // extras
          "", // fq
          "", // bps
          "t1,c1", // linkmodes
          ""); // command

    testd("auto:Makefile:c1,t1", false,
          "", // alias
          "", // file
          "", // type
          "", // id
          "", // extras
          "", // fq
          "", // bps
          "none", // linkmodes
          ""); // command

    testd("Vatten", false,
          "", // alias
          "", // file
          "", // type
          "", // id
          "", // extras
          "", // fq
          "", // bps
          "none", // linkmodes
          ""); // command

    testd("main=/dev/ttyUSB0:mbus:2400", true,
          "main", // alias
          "/dev/ttyUSB0", // file
          "mbus", // type
          "", // id
          "", // extras
          "", // fq
          "2400", // bps
          "none", // linkmodes
          ""); // command

    // Support : inside the command.
    testd("cul:c1:CMD(socat TCP:CUNO:2323 STDIO)", true,
          "", // alias
          "", // file
          "cul", // type
          "", // id
          "", // extras
          "", // fq
          "", // bps
          "c1", // linkmodes
          "socat TCP:CUNO:2323 STDIO"); // command
}

void test_month(int y, int m, int day, int mdiff, string from, string to)
{
    struct tm date {};
    date.tm_year  = y-1900;
    date.tm_mon   = m-1;
    date.tm_mday  = day;

    string s = strdate(&date);

    struct tm d;
    d = date;
    addMonths(&d, mdiff);

    string os = strdate(&d);

    if (s != from ||
        os != to)
    {
        printf("ERROR! Expected %s + %d months should be %s\n"
               "But got %s - 11 = %s\n",
               from.c_str(), mdiff, to.c_str(),
               s.c_str(), os.c_str());
    }
}

void test_months()
{
    test_month(2020,12,31, 2, "2020-12-31", "2021-02-28");
    test_month(2020,12,31,-10, "2020-12-31", "2020-02-29");
    test_month(2021,01,31,-1,  "2021-01-31", "2020-12-31");
    test_month(2021,01,31,-2,  "2021-01-31", "2020-11-30");
    test_month(2021,01,31,-24, "2021-01-31", "2019-01-31");
    test_month(2021,01,31, 24, "2021-01-31", "2023-01-31");
    test_month(2021,01,31, 22, "2021-01-31", "2022-11-30");

    // 2020 was a leap year.
    test_month(2021,02,28, -12, "2021-02-28", "2020-02-29");
    // 2000 was a leap year %100=0 but %400=0 overrides.
    test_month(2001,02,28, -12, "2001-02-28", "2000-02-29");
    // 2100 is not a leap year since %100=0 and not overriden %400 != 0.
    test_month(2000,02,29, 12*100, "2000-02-29", "2100-02-28");
}

// Vatten    multical21:BUS1:c1 12345678 KEY
// Tempmeter piigth(info=123):BUS2:2400   0        NOKEY

void testm(string arg, bool xok,
           string xdriver, string xextras, string xbus, string xbps, string xlm)
{
    MeterInfo mi;
    bool ok = mi.parse("", arg, "12345678", "");
    if (ok != xok)
    {
        printf("ERROR in meter parse test \"%s\" expected %s but got %s\n", arg.c_str(), xok?"OK":"FALSE", ok?"OK":"FALSE");
        return;
    }
    if (ok == false) return;

    bool driver_ok = mi.driverName().str() == xdriver;
    bool extras_ok = mi.extras == xextras;
    bool bus_ok = mi.bus == xbus;
    bool bps_ok = to_string(mi.bps) == xbps;
    bool link_modes_ok =  mi.link_modes.hr() == xlm;

    if (!driver_ok || !extras_ok || !bus_ok || !bps_ok || !link_modes_ok)
    {
        printf("ERROR in meterc parsing parts \"%s\" got\n"
               "driver: \"%s\", extras: \"%s\", bus: \"%s\", bbps: \"%s\", linkmodes: \"%s\"\n"
               "but expected\n"
               "driver: \"%s\", extras: \"%s\", bus: \"%s\", bbps: \"%s\", linkmodes: \"%s\"\n",

               arg.c_str(),

               mi.driverName().str().c_str(),
               mi.extras.c_str(),
               mi.bus.c_str(),
               to_string(mi.bps).c_str(),
               mi.link_modes.hr().c_str(),

               xdriver.c_str(),
               xextras.c_str(),
               xbus.c_str(),
               xbps.c_str(),
               xlm.c_str()
            );
    }
}

void testc(string file, string file_content,
    string xdriver, string xextras, string xbus, string xbps, string xlm)
{
    MeterInfo mi;
    Configuration c;

    vector<char> meter_conf(file_content.begin(), file_content.end());
    meter_conf.push_back('\n');

    parseMeterConfig(&c, meter_conf, file);

    assert(c.meters.size() > 0);

    mi = c.meters.back();

    if (mi.driverName().str() != xdriver ||
        mi.extras != xextras ||
        mi.bus != xbus ||
        to_string(mi.bps) != xbps ||
        mi.link_modes.hr() != xlm)
    {
        printf("ERROR in meterc parsing parts \"%s\" got\n"
               "driver: \"%s\", extras: \"%s\", bus: \"%s\", bbps: \"%s\", linkmodes: \"%s\"\n"
               "but expected\n"
               "driver: \"%s\", extras: \"%s\", bus: \"%s\", bbps: \"%s\", linkmodes: \"%s\"\n",

               file.c_str(),

               mi.driverName().str().c_str(),
               mi.extras.c_str(),
               mi.bus.c_str(),
               to_string(mi.bps).c_str(),
               mi.link_modes.hr().c_str(),

               xdriver.c_str(),
               xextras.c_str(),
               xbus.c_str(),
               xbps.c_str(),
               xlm.c_str()
            );
    }
}

void test_meters()
{
    string config_content;

    testm("piigth:BUS1:2400", true,
          "piigth", // driver
          "", // extras
          "BUS1", // bus
          "2400", // bps
          "none"); // linkmodes

    testm("c5isf:MAINO:9600:mbus", true,
          "c5isf", // driver
          "", // extras
          "MAINO", // bus
          "9600", // bps
          "mbus"); // linkmodes

    testm("c5isf:DONGLE:t1", true,
          "c5isf", // driver
          "", // extras
          "DONGLE", // bus
          "0", // bps
          "t1"); // linkmodes

    testm("c5isf:t1,c1,mbus", true,
          "c5isf", // driver
          "", // extras
          "", // bus
          "0", // bps
          "mbus,t1,c1"); // linkmodes

    /*
    config_content =
        "name=test\n"
        "driver=piigth:BUS1:2400:mbus\n"
        "id=01234567\n";

    testc("meter/piigth:BUS1:2400", config_content,
          "piigth", // driver
          "", // extras
          "BUS1", // bus
          "2400", // bps
          "mbus"); // linkmodes)
    */

    testm("multical21:c1", true,
          "multical21", // driver
          "", // extras
          "", // bus
          "0", // bps
          "c1"); // linkmodes

    config_content =
        "name=test\n"
        "driver=multical21:c1\n"
        "id=01234567\n";
    testc("meter/multical21:c1", config_content,
          "multical21", // driver
          "", // extras
          "", // bus
          "0", // bps
          "c1"); // linkmodes)


    testm("apator162(offset=162)", true,
          "apator162", // driver
          "offset=162", // extras
          "", // bus
          "0", // bps
          "none"); // linkmodes

    config_content =
        "name=test\n"
        "driver=apator162(offset=99)\n"
        "id=01234567\n"
        "key=00000000000000000000000000000000\n";
    testc("meter/apatortest", config_content,
          "apator162", // driver
          "offset=99", // extras
          "", // bus
          "0", // bps
          "none"); // linkmodes)

}

void tests(string arg, bool expect, LinkMode link_mode, TelegramFormat format, string bus, string content)
{
    SendBusContent sbc;
    bool rc = sbc.parse(arg);
    if (rc != expect && rc == false)
    {
        printf("ERROR could not parse send bus content \"%s\"\n", arg.c_str());
        return;
    }
    if (rc != expect && rc == true)
    {
        printf("ERROR could parse send bus content \"%s\" but expected failure!\n", arg.c_str());
        return;
    }

    if (expect == false && rc == false) return; // It failed, which was expected.

    if (sbc.link_mode != link_mode ||
        sbc.format != format ||
        sbc.bus != bus ||
        sbc.content != content)
    {
        printf("ERROR in parsing send bus content \"%s\"\n"
               "got      (link_mode: %s format: %s bus: %s, data: %s)\n"
               "expected (link_mode: %s format: %s bus: %s, data: %s)\n", arg.c_str(),
               toString(sbc.link_mode), toString(sbc.format), sbc.bus.c_str(), sbc.content.c_str(),
               toString(link_mode), toString(format), bus.c_str(), content.c_str());
    }
}

void test_sbc()
{
    tests("send:t1:wmbus_c_field:BUS1:11223344", true,
          LinkMode::T1,
          TelegramFormat::WMBUS_C_FIELD,
          "BUS1", // bus
          "11223344"); // content

    tests("send:c1:wmbus_ci_field:alfa:11", true,
          LinkMode::C1,
          TelegramFormat::WMBUS_CI_FIELD,
          "alfa", // bus
          "11"); // content

    tests("send:t2:wmbus_c_field:OUTBUS:1122334455", true,
          LinkMode::T2,
          TelegramFormat::WMBUS_C_FIELD,
          "OUTBUS", // bus
          "1122334455"); // content

    tests("alfa:t1", false, LinkMode::UNKNOWN, TelegramFormat::UNKNOWN, "", "");
    tests("send", false, LinkMode::UNKNOWN, TelegramFormat::UNKNOWN, "", "");
    tests("send:::::::::::", false, LinkMode::UNKNOWN, TelegramFormat::UNKNOWN, "", "");
    tests("send:foo", false, LinkMode::UNKNOWN, TelegramFormat::UNKNOWN, "", "");
    tests("send:t2:wmbus_c_field:OUT:", false, LinkMode::UNKNOWN, TelegramFormat::UNKNOWN, "", "");
    tests("send:t2:wmbus_c_field:OUT:1", false, LinkMode::UNKNOWN, TelegramFormat::UNKNOWN, "", "");

    tests("send:mbus:mbus_short_frame:out:5b00", true,
          LinkMode::MBUS,
          TelegramFormat::MBUS_SHORT_FRAME,
          "out", // bus
          "5b00"); // content

    tests("send:mbus:mbus_long_frame:mbus2:1122334455", true,
          LinkMode::MBUS,
          TelegramFormat::MBUS_LONG_FRAME,
          "mbus2", // bus
          "1122334455"); // content*/
}

void test_aes()
{
    vector<uchar> key;

    hex2bin("0123456789abcdef0123456789abcdef", &key);
    string poe =
        "Once upon a midnight dreary, while I pondered, weak and weary,\n"
        "Over many a quaint and curious volume of forgotten lore\n";

    while (poe.length() % 16 != 0)
    {
        poe += ".";
    }

    uchar iv[16];
    memset(iv, 0xaa, 16);

    uchar in[poe.length()];
    memcpy(in, &poe[0], poe.size());

    debug("(aes) input: \"%s\"\n", poe.c_str());

    uchar out[sizeof(in)];
    AES_CBC_encrypt_buffer(out, in, sizeof(in), safeButUnsafeVectorPtr(key), iv);

    vector<uchar> outv(out, out+sizeof(out));
    string s = bin2hex(outv);
    debug("(aes) encrypted: \"%s\"\n", s.c_str());

    uchar back[sizeof(in)];
    AES_CBC_decrypt_buffer(back, out, sizeof(in), safeButUnsafeVectorPtr(key), iv);

    string b = string(back, back+sizeof(back));
    debug("(aes) decrypted: \"%s\"\n", b.c_str());

    if (poe != b)
    {
        printf("ERROR! aes with IV encrypt decrypt failed!\n");
    }

    AES_ECB_encrypt(in, safeButUnsafeVectorPtr(key), out, sizeof(in));
    AES_ECB_decrypt(out, safeButUnsafeVectorPtr(key), back, sizeof(in));

    if (memcmp(back, in, sizeof(in)))
    {
        printf("ERROR! aes encrypt decrypt (no iv) failed!\n");
    }
}

void test_is_hex(const char *hex, bool expected_ok, bool expected_invalid, bool strict)
{
    bool got_invalid;
    bool got_ok;
    if (strict) got_ok = isHexStringStrict(hex, &got_invalid);
    else got_ok = isHexStringFlex(hex, &got_invalid);

    if (got_ok != expected_ok || got_invalid != expected_invalid)
    {
        printf("ERROR! hex string %s was expected to be %d (invalid %d) but got %d (invalid %d)\n",
               hex,
               expected_ok, expected_invalid, got_ok, got_invalid);
    }
}

void test_hex()
{
    test_is_hex("00112233445566778899aabbccddeeff", true, false, true);
    test_is_hex("00112233445566778899AABBCCDDEEFF", true, false, true);
    test_is_hex("00112233445566778899AABBCCDDEEF", true, true, true);
    test_is_hex("00112233445566778899AABBCCDDEEFG", false, false, true);

    test_is_hex("00 11 22 33#44|55#66 778899aabbccddeeff", true, false, false);
    test_is_hex("00 11 22 33#4|55#66 778899aabbccddeeff", true, true, false);
}

void test_translate()
{
    Translate::Lookup lookup1 =
        Translate::Lookup()
        .add(Translate::Rule("ACCESS_BITS", Translate::MapType::BitToString)
             .set(MaskBits(0xf0))
             .add(Translate::Map(0x10, "NO_ACCESS", TestBit::Set))
             .add(Translate::Map(0x20, "ALL_ACCESS", TestBit::Set))
             .add(Translate::Map(0x40, "TEMP_ACCESS", TestBit::Set))
            )
        .add(Translate::Rule("ACCESSOR_TYPE", Translate::MapType::IndexToString)
             .set(MaskBits(0x0f))
             .add(Translate::Map(0x00, "ACCESSOR_RED", TestBit::Set))
             .add(Translate::Map(0x07, "ACCESSOR_GREEN", TestBit::Set))
            )
        ;

   Translate::Lookup lookup2 =
        {
            {
                {
                    "FLOW_FLAGS",
                    Translate::MapType::BitToString,
                    AlwaysTrigger,
                    MaskBits(0x3f),
                    "OOOK",
                    {
                        { 0x01, "BACKWARD_FLOW" },
                        { 0x02, "DRY" },
                        { 0x10, "TRIG" },
                        { 0x20, "COS" },
                    }
                },
            },
        };

   Translate::Lookup lookup3 =
       {
            {
                {
                    "NO_FLAGS",
                    Translate::MapType::BitToString,
                    AlwaysTrigger,
                    MaskBits(0x03),
                    "OK",
                    {
                        // Test that 0x01 is set, means OK (ie installed)
                        // Not set means not installed.
                        { 0x01, "NOT_INSTALLED", TestBit::NotSet },
                        { 0x02, "FOO" },
                    }
                },
            },
        };

    string s, e;
    uint8_t bits;

    bits = 0xa0;
    s = sortStatusString(lookup1.translate(bits));
    e = sortStatusString("ALL_ACCESS ACCESS_BITS_80 ACCESSOR_RED");
    if (s != e)
    {
        printf("ERROR lookup1 0x%02x expected \"%s\" but got \"%s\"\n", bits, e.c_str(), s.c_str());
    }

    bits = 0x35;
    s = sortStatusString(lookup1.translate(bits));
    e = sortStatusString("NO_ACCESS ALL_ACCESS ACCESSOR_TYPE_5");
    if (s != e)
    {
        printf("ERROR lookup1 0x%02x expected \"%s\" but got \"%s\"\n", bits, e.c_str(), s.c_str());
    }

    bits = 0x02;
    s = lookup2.translate(0x02);
    e = "DRY";
    if (s != e)
    {
        printf("ERROR lookup2 0x%02x expected \"%s\" but got \"%s\"\n", bits, e.c_str(), s.c_str());
    }

    bits = 0x00;
    s = lookup2.translate(bits);
    e = "OOOK";
    if (s != e)
    {
        printf("ERROR lookup2 0x%02x expected \"%s\" but got \"%s\"\n", bits, e.c_str(), s.c_str());
    }

    // Verify that the not set 0x01 bit translates to NOT_INSTALLED
    // The set bit 0x02 translates to FOO.
    bits = 0x02;
    s = sortStatusString(lookup3.translate(0x02));
    e = sortStatusString("NOT_INSTALLED FOO");
    if (s != e)
    {
        printf("ERROR lookup3 0x%02x expected \"%s\" but got \"%s\"\n", bits, e.c_str(), s.c_str());
    }

    bits = 0x01;
    s = lookup3.translate(bits);
    e = "OK";
    if (s != e)
    {
        printf("ERROR lookup3 0x%02x expected \"%s\" but got \"%s\"\n", bits, e.c_str(), s.c_str());
    }

}

void test_slip()
{
    vector<uchar> from = { 1, 0xc0, 3, 4, 5, 0xdb };
    vector<uchar> expected_to = { 0xc0, 1, 0xdb, 0xdc, 3, 4, 5, 0xdb, 0xdd, 0xc0 };
    vector<uchar> to;
    vector<uchar> back;

    addSlipFraming(from, to);

    if (expected_to != to)
    {
        printf("ERROR slip 1\n");
    }

    size_t frame_length = 0;
    removeSlipFraming(to, &frame_length, back);

    if (back != from)
    {
        printf("ERROR slip 2\n");
    }

    if (to.size() != frame_length)
    {
        printf("ERROR slip 3\n");
    }

    vector<uchar> more = { 0xc0, 0xc0, 0xc0, 1, 2, 3, 4, 5, 6, 7, 8 };
    addSlipFraming(more, to);

    frame_length = 0;
    removeSlipFraming(to, &frame_length, back);

    if (back != from)
    {
        printf("ERROR slip 4\n");
    }

    to.erase(to.begin(), to.begin()+frame_length);
    removeSlipFraming(to, &frame_length, back);

    if (back != more)
    {
        printf("ERROR slip 5\n");
    }

    vector<uchar> again = { 0xc0 };
    removeSlipFraming(again, &frame_length, back);

    if (frame_length != 0)
    {
        printf("ERROR slip 6\n");
    }

    vector<uchar> againn = { 0xc0, 1, 2, 3, 4, 5 };
    removeSlipFraming(againn, &frame_length, back);

    if (frame_length != 0)
    {
        printf("ERROR slip 7\n");
    }

}

void test_dvs()
{
    DifVifKey dvk("0B2B");

    if (dvk.dif() != 0x0b || dvk.vif() != 0x2b || dvk.hasDifes() || dvk.hasVifes())
    {
        printf("ERROR test_dvs 1\n");
    }
}

void test_ascii_detection()
{
    string s = "000008";
    if (isLikelyAscii(s))
    {
        printf("ERROR >%s< should not be likely ascii\n", s.c_str());
    }
    s = "41424344";
    if (!isLikelyAscii(s))
    {
        printf("ERROR >%s< should be likely ascii\n", s.c_str());
    }
    s = "000041424344";
    if (!isLikelyAscii(s))
    {
        printf("ERROR >%s< should be likely ascii\n", s.c_str());
    }
    s = "000041194300";
    if (isLikelyAscii(s))
    {
        printf("ERROR >%s< should not be likely ascii\n", s.c_str());
    }
}

void test_join(string a, string b, string s)
{
    string t = joinStatusOKStrings(a, b);
    if (t != s)
    {
        printf("Expected joinStatusString(\"%s\",\"%s\") to be \"%s\" but got \"%s\"\n",
               a.c_str(), b.c_str(), s.c_str(), t.c_str());
    }

}

void test_status_join()
{
    test_join("OK", "OK", "OK");
    test_join("", "", "OK");
    test_join("OK", "", "OK");
    test_join("", "OK", "OK");
    test_join("null", "OK", "OK");
    test_join("null", "null", "OK");
    test_join("ERROR FLOW", "OK", "ERROR FLOW");
    test_join("ERROR FLOW", "", "ERROR FLOW");
    test_join("OK", "ERROR FLOW", "ERROR FLOW");
    test_join("", "ERROR FLOW", "ERROR FLOW");
    test_join("ERROR", "FLOW", "ERROR FLOW");
    test_join("ERROR", "null", "ERROR");
    test_join("A B C", "D E F G", "A B C D E F G");
}

void test_sort(string in, string out)
{
    string t = sortStatusString(in);
    if (t != out)
    {
        printf("Expected sortStatusString(\"%s\") to be \"%s\" but got \"%s\"\n",
               in.c_str(), out.c_str(), t.c_str());
    }
}

void test_status_sort()
{
    test_sort("C B A", "A B C");
    test_sort("ERROR BUSY FLOW ERROR", "BUSY ERROR FLOW");
    test_sort("X X X Y Y Z A B C A A AAAA AA AAA", "A AA AAA AAAA B C X Y Z");
}

void test_field_matcher()
{
    // 04 dif (32 Bit Integer/Binary Instantaneous value)
    // 13 vif (Volume l)
    // 2F4E0000 ("total_m3":20.015)

    FieldMatcher m1 = FieldMatcher::build()
        .set(MeasurementType::Instantaneous)
        .set(VIFRange::Volume);

    string v1 = "2F4E0000";
    DVEntry e1(0,
               DifVifKey("0413"),
               MeasurementType::Instantaneous,
               Vif(0x13),
               { },
               { },
               StorageNr(0),
               TariffNr(0),
               SubUnitNr(0),
               v1);

    if (!m1.matches(e1))
    {
        printf("ERROR expected match for field matcher test 1 !\n");
    }

    // 81 dif (8 Bit Integer/Binary Instantaneous value)
    // 01 dife (subunit=0 tariff=0 storagenr=2)
    // 10 vif (Volume)
    // FC combinable vif (Extension)
    // 0C combinable vif (DeltaBetween...)
    // 03 ("external_temperature_c":3)

    FieldMatcher m2 = FieldMatcher::build()
        .set(MeasurementType::Instantaneous)
        .set(StorageNr(2))
        .set(VIFRange::Volume)
        .add(VIFCombinable::Any);

    string v2 = "03";
    DVEntry e2(0,
               DifVifKey("810110FC0C"),
               MeasurementType::Instantaneous,
               Vif(0x10),
               { VIFCombinable::DeltaBetweenImportAndExport },
               { },
               StorageNr(2),
               TariffNr(0),
               SubUnitNr(0),
               v2);

    if (!m2.matches(e2))
    {
        printf("ERROR expected match for field matcher test 2 !\n");
    }

    FieldMatcher m3 = FieldMatcher::build()
        .set(MeasurementType::Instantaneous)
        .set(StorageNr(2))
        .set(VIFRange::Volume)
        .add(VIFCombinable::DeltaBetweenImportAndExport);

    if (!m3.matches(e2))
    {
        printf("ERROR expected match for field matcher test 3 !\n");
    }

    FieldMatcher m4 = FieldMatcher::build()
        .set(MeasurementType::Instantaneous)
        .set(StorageNr(2))
        .set(VIFRange::Volume)
        .add(VIFCombinable::ValueDuringUpperLimitExceeded);

    if (m4.matches(e2))
    {
        printf("ERROR expected NO match for field matcher test 4 !\n");
    }

}

void test_unit(string in, bool expected_ok, string expected_vname, Unit expected_unit)
{
    Unit unit;
    string vname;

    bool ok = extractUnit(in, &vname, &unit);

    if (ok != expected_ok ||
        vname != expected_vname ||
        unit != expected_unit)
    {
        printf("ERROR expected ok=%d vname=%s unit=%s but got\n"
               "      but got  ok=%d vname=%s unit=%s\n",
               expected_ok, expected_vname.c_str(), unitToStringUpperCase(expected_unit).c_str(),
               ok, vname.c_str(), unitToStringUpperCase(unit).c_str());
    }
}

void test_units_extraction()
{
    test_unit("total_kwh", true, "total", Unit::KWH);
    test_unit("total_", false, "", Unit::Unknown);
    test_unit("total", false, "", Unit::Unknown);
    test_unit("", false, "", Unit::Unknown);
    test_unit("_c", false, "", Unit::Unknown);

    test_unit("work__c", true, "work_", Unit::C);

    test_unit("water_c", true, "water", Unit::C);
    test_unit("walk_counter", true, "walk", Unit::COUNTER);
    test_unit("work_kvarh", true, "work", Unit::KVARH);

    test_unit("current_power_consumption_phase1_kw", true, "current_power_consumption_phase1", Unit::KW);
}

void test_expected_failed_si_convert(Unit from_unit,
                                     Unit to_unit,
                                     Quantity q)
{
    SIUnit from_si_unit(from_unit);
    SIUnit to_si_unit(to_unit);
    string fu = unitToStringLowerCase(from_si_unit.asUnit());
    string tu = unitToStringLowerCase(to_si_unit.asUnit());

    if (q != from_si_unit.quantity() || q != to_si_unit.quantity())
    {
        printf("ERROR! Not the expected quantities!\n");
    }
    if (from_si_unit.convertTo(0, to_si_unit, NULL))
    {
        printf("ERROR! Should not be able to convert from %s to %s !\n", fu.c_str(), tu.c_str());
    }
}

void test_si_convert(double from_value, double expected_value,
                     Unit from_unit,
                     string expected_from_unit,
                     Unit to_unit,
                     string expected_to_unit,
                     Quantity q,
                     set<Unit> *from_set,
                     set<Unit> *to_set)
{
    debug("test_si_convert from %.17g %s to %.17g %s\n",
          from_value, expected_from_unit.c_str(),
          expected_value, expected_to_unit.c_str());

    string evs = tostrprintf("%.15g", expected_value);

    SIUnit from_si_unit(from_unit);
    SIUnit to_si_unit(to_unit);
    string fu = unitToStringLowerCase(from_si_unit.asUnit(q));
    string tu = unitToStringLowerCase(to_si_unit.asUnit(q));

    from_set->erase(from_unit);
    to_set->erase(to_unit);

    double e {};
    from_si_unit.convertTo(from_value, to_si_unit, &e);
    string es = tostrprintf("%.15g", e);

    if (canConvert(from_unit, to_unit))
    {
        // Test if conversion was the same using 16 significant digits.
        // I.e. slightly less than the maximum 17 significant digits.
        // Takes up the slack between the old style conversion and the new style conversion
        // which can introduce minor changes in the final digit.
        double ee = convert(from_value, from_unit, to_unit);
        string ees = tostrprintf("%.15g", ee);
        if (es != ees)
        {
            printf("ERROR! SI unit conversion %.15g (%s) from %.15g differs from unit conversion %.15g (%s)! \n",
                   e, es.c_str(), from_value, ee, ees.c_str());
        }
    }
    if (fu != expected_from_unit)
    {
        printf("ERROR! Expected from unit %s (but got %s) when converting si unit %s\n",
               expected_from_unit.c_str(), fu.c_str(), from_si_unit.str().c_str());
    }
    if (tu != expected_to_unit)
    {
        printf("ERROR! Expected to unit %s (but got %s) when converting si unit %s\n",
               expected_to_unit.c_str(), tu.c_str(), to_si_unit.str().c_str());
    }
    if (es != evs)
    {
        printf("ERROR! Expected %.17g [%s] (but got %.17g [%s]) when converting %.17g from %s (%s) to %s (%s)\n",
               expected_value, evs.c_str(), e, es.c_str(), from_value,
               from_si_unit.str().c_str(),
               fu.c_str(),
               to_si_unit.str().c_str(),
               tu.c_str());
    }
}

void test_si_units_siexp()
{
    // m3/s
    SIExp e = SIExp::build().s(-1).m(3);
    if (e.str() != "m³s⁻¹") { printf("ERROR Expected m³s⁻¹ but got \"%s\"\n", e.str().c_str()); }

    SIExp f = SIExp::build().s(1);
    if (f.str() != "s") { printf("ERROR Expected s but got \"%s\"\n", f.str().c_str()); }

    SIExp g = e.mul(f);
    if (g.str() != "m³") { printf("ERROR Expected m³ but got \"%s\"\n", g.str().c_str()); }

    SIExp h = SIExp::build().s(127);

    // Test overflow of exponent for seconds!
    SIExp i = h.mul(f);
    if (i.str() != "!s⁻¹²⁸-Invalid!") { printf("ERROR Expected !s⁻¹²⁸-Invalid! but got \"%s\"\n", i.str().c_str()); }

    SIExp j = e.div(e);
    if (j.str() != "") { printf("ERROR Expected \"\" but got \"%s\"\n", j.str().c_str()); }

    SIExp bad = SIExp::build().k(1).c(1);
    if (bad.str() != "!kc-Invalid!") { printf("ERROR Expected !kc-Invalid! but got \"%s\"\n", bad.str().c_str()); }

}

void test_si_units_basic()
{
    // A kilowatt unit generated from scratch:
    SIUnit kwh(Quantity::Energy, 3.6E6, SIExp().kg(1).m(2).s(-2));

    string expected = "3.6×10⁶kgm²s⁻²";
    if (kwh.str() != expected) printf("ERROR expected kwh to be %s but got %s\n", expected.c_str(), kwh.str().c_str());

    // A kilowatt unit from the unit lookup table.
    SIUnit kwh2(Unit::KWH);

    if (kwh2.str() != expected) printf("ERROR expected second kwh to be %s but got %s\n", expected.c_str(), kwh2.str().c_str());

    // A Celsius unit generated from scratch:
    SIUnit celsius(Quantity::Temperature, 1, SIExp().c(1));

    expected = "1c";
    if (celsius.str() != expected) printf("ERROR expected celsius to be %s but got %s\n", expected.c_str(), celsius.str().c_str());

    // A celsius unit from the Unit.
    SIUnit celsius2(Unit::C);

    if (celsius2.str() != expected) printf("ERROR expected second celsius to be %s but got %s\n", expected.c_str(), celsius2.str().c_str());
}

void fill_with_units_from(Quantity q, set<Unit> *s)
{
    s->clear();
#define X(cname,lcname,hrname,quantity,explanation) if (q == Quantity::quantity) s->insert(Unit::cname);
LIST_OF_UNITS
#undef X
}

void check_units_tested(set<Unit> &from_set, set<Unit> &to_set, Quantity q)
{
    if (from_set.size() > 0)
    {
        printf("ERROR not all units as source in quantity %s tested! Remaining: ", toString(q));
        for (Unit u : from_set) printf("%s ", unitToStringLowerCase(u).c_str());
        printf("\n");
    }
    if (to_set.size() > 0)
    {
        printf("ERROR not all units as targets in quantity %s tested! Remaining: ", toString(q));
        for (Unit u : to_set) printf("%s ", unitToStringLowerCase(u).c_str());
        printf("\n");
    }
}

void check_quantities_tested(set<Quantity> &s)
{
    if (s.size() > 0)
    {
        printf("ERROR not all quantities tested! Remaining: ");
        for (Quantity q : s) printf("%s ", toString(q));
        printf("\n");
    }
}

void test_si_units_conversion()
{
    set<Quantity> q_set;
    set<Unit> from_set;
    set<Unit> to_set;

#define X(quantity,default_unit) q_set.insert(Quantity::quantity);
LIST_OF_QUANTITIES
#undef X

    // Test time units: s, min, h, d, y
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::Time);
    fill_with_units_from(Quantity::Time, &from_set);
    fill_with_units_from(Quantity::Time, &to_set);

    // 60 seconds is one minute.
    test_si_convert(60.0, 1.0, Unit::Second, "s", Unit::Minute, "min", Quantity::Time, &from_set, &to_set);
    // 3600 seconds is one hour.
    test_si_convert(3600.0, 1.0, Unit::Second, "s", Unit::Hour, "h", Quantity::Time, &from_set, &to_set);
    // 3600 seconds is 1/24 of a day which is 0.041666666666666664.
    test_si_convert(3600.0, 0.041666666666666664, Unit::Second, "s", Unit::Day, "d", Quantity::Time, &from_set, &to_set);
    // Same test again.
    test_si_convert(3600.0, 1.0/24.0, Unit::Second, "s", Unit::Day, "d", Quantity::Time, &from_set, &to_set);
    // 1 min is 60 seconds.
    test_si_convert(1.0, 60.0, Unit::Minute, "min", Unit::Second, "s", Quantity::Time, &from_set, &to_set);
    // 1 day is 24 hours
    test_si_convert(1.0, 24, Unit::Day, "d", Unit::Hour, "h", Quantity::Time, &from_set, &to_set);
    // 1 month is 1 month.
    test_si_convert(1.0, 1.0, Unit::Month, "month", Unit::Month, "month", Quantity::Time, &from_set, &to_set);
    test_si_convert(1.0, 1.0, Unit::Year, "y", Unit::Year, "y", Quantity::Time, &from_set, &to_set);
    // 100 hours is 100/24 days.
    test_si_convert(100.0, 100.0/24.0, Unit::Hour, "h", Unit::Day, "d", Quantity::Time, &from_set, &to_set);
    // 1 year is 365.2425 days.
//    test_si_convert(1.0, 365.2425, Unit::Year, "y", Unit::Day, "d", Quantity::Time, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::Time);

    // Test length units: m
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::Length);
    fill_with_units_from(Quantity::Length, &from_set);
    fill_with_units_from(Quantity::Length, &to_set);

    test_si_convert(111.1, 111.1, Unit::M, "m", Unit::M, "m", Quantity::Length, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::Length);

    // Test mass units: kg
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::Mass);
    fill_with_units_from(Quantity::Mass, &from_set);
    fill_with_units_from(Quantity::Mass, &to_set);

    test_si_convert(222.1, 222.1, Unit::KG, "kg", Unit::KG, "kg", Quantity::Mass, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::Mass);

    // Test electrical current units: a
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::Amperage);
    fill_with_units_from(Quantity::Amperage, &from_set);
    fill_with_units_from(Quantity::Amperage, &to_set);

    test_si_convert(999.9, 999.9, Unit::Ampere, "a", Unit::Ampere, "a", Quantity::Amperage, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::Amperage);

    // Test temperature units: c k f
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::Temperature);
    fill_with_units_from(Quantity::Temperature, &from_set);
    fill_with_units_from(Quantity::Temperature, &to_set);

    test_si_convert(0, 273.15, Unit::C, "c", Unit::K, "k", Quantity::Temperature, &from_set, &to_set);
    test_si_convert(10.85, 284.0, Unit::C, "c", Unit::K, "k", Quantity::Temperature, &from_set, &to_set);
    test_si_convert(100.0, -173.15, Unit::K, "k", Unit::C, "c", Quantity::Temperature, &from_set, &to_set);
    test_si_convert(100.0, -279.67, Unit::K, "k", Unit::F, "f", Quantity::Temperature, &from_set, &to_set);
    test_si_convert(100.0, 37.77777777777777, Unit::F, "f", Unit::C, "c", Quantity::Temperature, &from_set, &to_set);
    test_si_convert(0.0, -17.7777777777778, Unit::F, "f", Unit::C, "c", Quantity::Temperature, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::Temperature);


    // Test energy units: kwh, mj, gj
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::Energy);
    fill_with_units_from(Quantity::Energy, &from_set);
    fill_with_units_from(Quantity::Energy, &to_set);

    // 1 kwh is 3.6 mj
    test_si_convert(1.0, 3.6, Unit::KWH, "kwh", Unit::MJ, "mj", Quantity::Energy, &from_set, &to_set);
    // 1 kwh is 0.0036 gj
    test_si_convert(1.0, 0.0036, Unit::KWH, "kwh", Unit::GJ, "gj", Quantity::Energy, &from_set, &to_set);
    // 1 gj is 1000 mj
    test_si_convert(1.0, 1000.0, Unit::GJ, "gj", Unit::MJ, "mj", Quantity::Energy, &from_set, &to_set);
    // 10 mj is 2.77777 kwh
    test_si_convert(10, 2.7777777777777777, Unit::MJ, "mj", Unit::KWH, "kwh", Quantity::Energy, &from_set, &to_set);
    // 1 ws = 1/3600000 kwh is 1 j = 0.000001 MJ
    test_si_convert(1.0/3600000.0, 0.000001, Unit::KWH, "kwh", Unit::MJ, "mj", Quantity::Energy, &from_set, &to_set);

    // 99 m3c = 99 m3c this is the only test we can do with the m3c energy unit,
    // which cannot be converted into other energy units since we lack the density of the water etc.
    test_si_convert(99.0, 99.0, Unit::M3C, "m3c", Unit::M3C, "m3c", Quantity::Energy, &from_set, &to_set);

    test_expected_failed_si_convert(Unit::M3C, Unit::KWH, Quantity::Energy);

    check_units_tested(from_set, to_set, Quantity::Energy);

    // Test reactive energy kvarh unit: kvarh
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::Reactive_Energy);
    fill_with_units_from(Quantity::Reactive_Energy, &from_set);
    fill_with_units_from(Quantity::Reactive_Energy, &to_set);

    // 1 kvarh is 1kwh
    test_si_convert(1.0, 1.0, Unit::KVARH, "kvarh", Unit::KWH, "kvarh", Quantity::Reactive_Energy, &from_set, &to_set);
    test_si_convert(1.0, 1.0, Unit::KWH, "kvarh", Unit::KVARH, "kvarh", Quantity::Reactive_Energy, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::Reactive_Energy);

    // Test apparent energy kvah unit: kvah
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::Apparent_Energy);
    fill_with_units_from(Quantity::Apparent_Energy, &from_set);
    fill_with_units_from(Quantity::Apparent_Energy, &to_set);

    // 1 kvah is 1kwh
    test_si_convert(1.0, 1.0, Unit::KVAH, "kvah", Unit::KWH, "kvah", Quantity::Apparent_Energy, &from_set, &to_set);
    test_si_convert(1.0, 1.0, Unit::KWH, "kvah", Unit::KVAH, "kvah", Quantity::Apparent_Energy, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::Apparent_Energy);

    // Test volume units: m3 l
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::Volume);
    fill_with_units_from(Quantity::Volume, &from_set);
    fill_with_units_from(Quantity::Volume, &to_set);

    test_si_convert(1, 1000.0, Unit::M3, "m3", Unit::L, "l", Quantity::Volume, &from_set, &to_set);
    test_si_convert(1, 1.0/1000.0, Unit::L, "l", Unit::M3, "m3", Quantity::Volume, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::Volume);

    // Test voltage unit: v
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::Voltage);
    fill_with_units_from(Quantity::Voltage, &from_set);
    fill_with_units_from(Quantity::Voltage, &to_set);

    test_si_convert(1, 1, Unit::Volt, "v", Unit::Volt, "v", Quantity::Voltage, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::Voltage);

    // Test power unit: kw
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::Power);
    fill_with_units_from(Quantity::Power, &from_set);
    fill_with_units_from(Quantity::Power, &to_set);

    test_si_convert(1, 1, Unit::KW, "kw", Unit::KW, "kw", Quantity::Power, &from_set, &to_set);
    // The power variant is m3ch.
    test_si_convert(99.0, 99.0, Unit::M3CH, "m3ch", Unit::M3CH, "m3ch", Quantity::Power, &from_set, &to_set);

    test_expected_failed_si_convert(Unit::M3CH, Unit::KW, Quantity::Power);

    check_units_tested(from_set, to_set, Quantity::Power);

    // Test volume flow units: m3h lh
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::Flow);
    fill_with_units_from(Quantity::Flow, &from_set);
    fill_with_units_from(Quantity::Flow, &to_set);

    test_si_convert(1, 1000.0, Unit::M3H, "m3h", Unit::LH, "lh", Quantity::Flow, &from_set, &to_set);
    test_si_convert(1000.0, 1.0, Unit::LH, "lh", Unit::M3H, "m3h", Quantity::Flow, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::Flow);

    // Test amount of substance: mol
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::AmountOfSubstance);
    fill_with_units_from(Quantity::AmountOfSubstance, &from_set);
    fill_with_units_from(Quantity::AmountOfSubstance, &to_set);

    test_si_convert(1.1717, 1.1717, Unit::MOL, "mol", Unit::MOL, "mol", Quantity::AmountOfSubstance, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::AmountOfSubstance);

    // Test luminous intensity: cd
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::LuminousIntensity);
    fill_with_units_from(Quantity::LuminousIntensity, &from_set);
    fill_with_units_from(Quantity::LuminousIntensity, &to_set);

    test_si_convert(1.1717, 1.1717, Unit::CD, "cd", Unit::CD, "cd", Quantity::LuminousIntensity, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::LuminousIntensity);

    // Test relative humidity: rh
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::RelativeHumidity);
    fill_with_units_from(Quantity::RelativeHumidity, &from_set);
    fill_with_units_from(Quantity::RelativeHumidity, &to_set);

    test_si_convert(1.1717, 1.1717, Unit::RH, "rh", Unit::RH, "rh", Quantity::RelativeHumidity, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::RelativeHumidity);

    // Test heat cost allocation: hca
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::HCA);
    fill_with_units_from(Quantity::HCA, &from_set);
    fill_with_units_from(Quantity::HCA, &to_set);

    test_si_convert(11717, 11717, Unit::HCA, "hca", Unit::HCA, "hca", Quantity::HCA, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::HCA);

    // Test pressure: bar pa
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::Pressure);
    fill_with_units_from(Quantity::Pressure, &from_set);
    fill_with_units_from(Quantity::Pressure, &to_set);

    test_si_convert(1.1717, 117170, Unit::BAR, "bar", Unit::PA, "pa", Quantity::Pressure, &from_set, &to_set);
    test_si_convert(1.1717, 1.1717e-05, Unit::PA, "pa", Unit::BAR, "bar", Quantity::Pressure, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::Pressure);

    // Test frequency: hz
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::Frequency);
    fill_with_units_from(Quantity::Frequency, &from_set);
    fill_with_units_from(Quantity::Frequency, &to_set);

    test_si_convert(440, 440, Unit::HZ, "hz", Unit::HZ, "hz", Quantity::Frequency, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::Frequency);

    // Test counter: counter
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::Dimensionless);
    fill_with_units_from(Quantity::Dimensionless, &from_set);
    fill_with_units_from(Quantity::Dimensionless, &to_set);

    test_si_convert(2211717, 2211717, Unit::COUNTER, "counter", Unit::FACTOR, "counter", Quantity::Dimensionless, &from_set, &to_set);
    test_si_convert(2211717, 2211717, Unit::FACTOR, "counter", Unit::COUNTER, "counter", Quantity::Dimensionless, &from_set, &to_set);
    test_si_convert(2211717, 2211717, Unit::NUMBER, "counter", Unit::COUNTER, "counter", Quantity::Dimensionless, &from_set, &to_set);
    test_si_convert(2211717, 2211717, Unit::FACTOR, "counter", Unit::NUMBER, "counter", Quantity::Dimensionless, &from_set, &to_set);
    test_si_convert(2211717, 2211717, Unit::PERCENTAGE, "counter", Unit::NUMBER, "counter", Quantity::Dimensionless, &from_set, &to_set);
    test_si_convert(2211717, 2211717, Unit::NUMBER, "counter", Unit::PERCENTAGE, "counter", Quantity::Dimensionless, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::Dimensionless);

    // Test angles: deg rad
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    q_set.erase(Quantity::Angle);
    fill_with_units_from(Quantity::Angle, &from_set);
    fill_with_units_from(Quantity::Angle, &to_set);

    test_si_convert(180, 3.1415926535897931l, Unit::DEGREE, "deg", Unit::RADIAN, "rad", Quantity::Angle, &from_set, &to_set);
    test_si_convert(3.1415926535897931l, 180, Unit::RADIAN, "rad", Unit::DEGREE, "deg", Quantity::Angle, &from_set, &to_set);

    check_units_tested(from_set, to_set, Quantity::Angle);

    // Test point in time units: ut utc lt
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    // I do not know how to handle the point in time units yet.
    // Mark them as tested....
    q_set.erase(Quantity::PointInTime);

    // Test text unit: text
    /////////////////////////////////////////////////////////////////////////////////////////////////////

    // I do not know how to handle the text unit yet.
    // Mark it as tested....
    q_set.erase(Quantity::Text);

    check_quantities_tested(q_set);
}


void test_formulas_building_consts()
{
    unique_ptr<FormulaImplementation> f = unique_ptr<FormulaImplementation>(new FormulaImplementation());
    double v, expected;

    f->doConstant(Unit::KWH, 17);
    f->doConstant(Unit::KWH, 1);
    f->doAddition(SI_KWH);
    v = f->calculate(Unit::KWH);
    if (v != 18.0)
    {
        printf("ERROR in test formula 1 expected 18.0 but got %lf\n", v);
    }

    f->clear();
    f->doConstant(Unit::KWH, 10);
    v = f->calculate(Unit::MJ);
    if (v != 36.0)
    {
        printf("ERROR in test formula 2 expected 36.0 but got %lf\n", v);
    }

    f->clear();
    f->doConstant(Unit::GJ, 10);
    f->doConstant(Unit::MJ, 10);
    f->doAddition(SI_GJ);
    v = f->calculate(Unit::GJ);
    if (v != 10.01)
    {
        printf("ERROR in test formula 3 expected 10.01 but got %lf\n", v);
    }

    f->clear();
    f->doConstant(Unit::C, 10);
    f->doConstant(Unit::C, 20);
    f->doAddition(SI_C);
    f->doConstant(Unit::C, 22);
    f->doAddition(SI_C);
    v = f->calculate(Unit::C);
    if (v != 52)
    {
        printf("ERROR in test formula 4 expected 52 but got %lf\n", v);
    }

    f->clear();
    f->doConstant(Unit::Month, 2);
    f->doConstant(Unit::COUNTER, 3);
    f->doMultiplication();
    v = f->calculate(Unit::Month);
    if (v != 6)
    {
        printf("ERROR in test formula 5 expected 6 but got %g\n", v);
    }

    f->clear();
    f->doConstant(Unit::UnixTimestamp, 3600*24*11); // mon 12 jan 1970 01:00:00 CET
    f->doConstant(Unit::Second, 9);
    f->doAddition(SIUnit(Unit::UnixTimestamp));
    v = f->calculate(Unit::UnixTimestamp);
    expected = 3600*24*11+9;
    if (v != expected)
    {
        printf("ERROR in test formula 6 expected %g but got %g\n", expected, v);
    }

    f->clear();
    f->doConstant(Unit::UnixTimestamp, 3600*24*11); // mon 12 jan 1970 01:00:00 CET
    f->doConstant(Unit::Month, 1);
    f->doAddition(SIUnit(Unit::UnixTimestamp));
    v = f->calculate(Unit::UnixTimestamp);
    expected = 3600*24*(31+11); // mon 12 feb 1970 01:00:00 CET
    if (v != expected)
    {
        printf("ERROR in test formula 7 expected %g but got %g\n", expected, v);
    }
}

void test_formulas_building_meters()
{
    unique_ptr<FormulaImplementation> f = unique_ptr<FormulaImplementation>(new FormulaImplementation());

    ////////////////////////////////////////////////////////////////////////////////////////////////////

    {
        MeterInfo mi;
        assert(lookupDriverInfo("multical21"));
        mi.parse("testur", "multical21", "12345678", "");
        shared_ptr<Meter> meter = createMeter(&mi);
        FieldInfo *fi_flow = meter->findFieldInfo("flow_temperature", Quantity::Temperature);
        FieldInfo *fi_ext = meter->findFieldInfo("external_temperature", Quantity::Temperature);
        assert(fi_flow != NULL);
        assert(fi_ext != NULL);

        vector<uchar> frame;
        hex2bin("2a442d2c785634121B168d2091d37cac217f2d7802ff207100041308190000441308190000615B1f616713", &frame);

        Telegram t;
        MeterKeys mk;
        t.parse(frame, &mk, true);

        vector<Address> addresses;
        bool match;
        meter->handleTelegram(t.about, frame, true, &addresses, &match, &t);

        f->clear();
        f->setMeter(meter.get());

        f->doMeterField(Unit::C, fi_flow);
        double v = f->calculate(Unit::C);
        if (v != 31)
        {
            printf("ERROR in test formula 5 expected 31 but got %lf\n", v);
        }

        f->clear();
        f->setMeter(meter.get());

        f->doMeterField(Unit::C, fi_flow);
        f->doMeterField(Unit::C, fi_ext);
        f->doAddition(SIUnit(Unit::C));
        v = f->calculate(Unit::C);
        if (v != 50)
        {
            printf("ERROR in test formula 6 expected 50 but got %lf\n", v);
        }

        // Check that trying to add a field reference expecting a non-convertible unit, will fail!
//        f->clear();
//        assert(false == f->doField(Unit::M3, meter.get(), fi_flow));
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////

    {
        MeterInfo mi;
        mi.parse("testur", "ebzwmbe", "22992299", "");
        shared_ptr<Meter> meter = createMeter(&mi);
        FieldInfo *fi_p1 = meter->findFieldInfo("current_power_consumption_phase1", Quantity::Power);
        FieldInfo *fi_p2 = meter->findFieldInfo("current_power_consumption_phase2", Quantity::Power);
        FieldInfo *fi_p3 = meter->findFieldInfo("current_power_consumption_phase3", Quantity::Power);
        assert(fi_p1 != NULL);
        assert(fi_p2 != NULL);
        assert(fi_p3 != NULL);

        vector<uchar> frame;
        hex2bin("5B445a149922992202378c20f6900f002c25Bc9e0000BBBBBBBBBBBBBBBB72992299225a140102f6003007102f2f040330f92a0004a9ff01ff24000004a9ff026a29000004a9ff03460600000dfd11063132333435362f2f2f2f2f2f", &frame);

        Telegram t;
        MeterKeys mk;
        t.parse(frame, &mk, true);

        vector<Address> id;
        bool match;
        meter->handleTelegram(t.about, frame, true, &id, &match, &t);

        unique_ptr<FormulaImplementation> f = unique_ptr<FormulaImplementation>(new FormulaImplementation());

        f->clear();
        f->setMeter(meter.get());

        f->doMeterField(Unit::KW, fi_p1);
        f->doMeterField(Unit::KW, fi_p2);
        f->doAddition(SI_KW);
        f->doMeterField(Unit::KW, fi_p3);
        f->doAddition(SI_KW);

        double v = f->calculate(Unit::KW);
        if (v != 0.21679)
        {
            printf("ERROR in test formula 7 expected 0.21679 but got %lf\n", v);
        }
    }
}

void test_formula_tree(FormulaImplementation *f, Meter *m, string formula, string tree)
{
    f->clear();
    f->parse(m, formula);
    string t = f->tree();
    if (t != tree)
    {
        printf("ERROR when parsing \"%s\" expected tree to be \"%s\"\nbut got \"%s\"\n",
               formula.c_str(), tree.c_str(), t.c_str());
    }
}

void test_formula_value(FormulaImplementation *f, Meter *m, string formula, double val, Unit unit)
{
    f->clear();

    bool ok = f->parse(m, formula);
    assert(ok);

    double v = f->calculate(unit);
    debug("(formula) %s\n", f->tree().c_str());

    if (v != val)
    {
        printf("ERROR when evaluating \"%s\"\nERROR expected %.17g but got %.17g\n", formula.c_str(), val, v);
    }
}

void test_formula_error(FormulaImplementation *f, Meter *m, string formula, Unit unit, string errors)
{
    f->clear();

    bool ok = f->parse(m, formula);
    string es = f->errors();
    if (es != errors)
    {
        printf("ERROR when parsing \"%s\"\nExpected errors:\n%sBut got errors:\n%s",
               formula.c_str(),
               errors.c_str(),
               f->errors().c_str());
    }
    assert(!ok);
}

double totime(int year, int month = 1, int day = 1, int hour = 0, int min = 0, int sec = 0)
{
    struct tm date {};

    date.tm_year  = year-1900;
    date.tm_mon   = month-1;
    date.tm_mday  = day;
    date.tm_hour = hour;
    date.tm_min = min;
    date.tm_sec = sec;

    // This t timestamp is dependent on the local time zone.
    time_t t = mktime(&date);
    /*
    // Extract the local time zone.
    struct tm tz_adjust {};
    localtime_r(&t, &tz_adjust);

    // if tm_gmtoff is zero, then we are in Greenwich!
    // if tm_gmtoff is 3600 then we are in Stockholm!
    // Now adjust the t timestamp so that we execute this this, as if we are in Greenwich.
    // This way, the test will work wherever in the world we run it.
    t -= tz_adjust.tm_gmtoff;
    */
    return (double)t;
}

void test_datetime(FormulaImplementation *f, string formula, int year, int month=1, int day=1, int hour=0, int min=0, int sec=0)
{
    f->clear();
    double expected = totime(year,month,day,hour,min,sec);
    f->parse(NULL, formula);
    if (!f->valid())
    {
        printf("%s\n", f->errors().c_str());
    }

    double v = f->calculate(Unit::UnixTimestamp);
    if (v != expected)
    {
        time_t t = v;
        struct tm time {};
        localtime_r(&t, &time);
        string gs = strdatetimesec(&time);

        printf("ERROR Expected datetime %.17g %04d-%02d-%02d %02d:%02d:%02d "
               "but got %.17g (%s) when testing \"%s\"\n",
               expected, year, month, day, hour, min, sec,
               v, gs.c_str(), formula.c_str());
    }
}

void test_time(FormulaImplementation *f, string formula, int hour=0, int min=0, int sec=0)
{
    f->clear();
    double expected = hour*3600+min*60+sec;
    f->parse(NULL, formula);
    if (!f->valid())
    {
        printf("%s\n", f->errors().c_str());
    }

    double v = f->calculate(Unit::Second);
    if (v != expected)
    {
        printf("ERROR Expected time %.17g but got %.17g when testing %s %02d:%02d.%02d\n",
               expected, v, formula.c_str(), hour, min, sec);
    }
}

void test_formulas_datetimes()
{
    unique_ptr<FormulaImplementation> f = unique_ptr<FormulaImplementation>(new FormulaImplementation());

    test_datetime(f.get(), "'2022-02-02'", 2022, 02, 02);
    test_datetime(f.get(), "'2021-02-28'", 2021, 02, 28);

    test_datetime(f.get(), "'1970-01-01 01:00:00'", 1970, 01, 01, 01, 00, 00);
    test_datetime(f.get(), "'1970-01-01 01:00'", 1970, 01, 01, 01, 00);
    test_datetime(f.get(), "'1970-01-01'", 1970, 01, 01);

    test_time(f.get(), "'00:15'", 0, 15, 0);
    test_time(f.get(), "'00:00:16'", 0, 0, 16);

    test_datetime(f.get(), "'2022-01-01 00:00:00' + 1s", 2022,1,1,0,0,1);
    test_datetime(f.get(), "'1971-10-01 02:17' +7d+1h+2min+1s", 1971, 10, 8, 3, 19, 1);

    test_datetime(f.get(), "'2000-01-01' + 1month", 2000, 2, 1);
    test_datetime(f.get(), "'2020-12-31' + 2month", 2021, 2, 28);
    test_datetime(f.get(), "'2020-12-31' - 10month", 2020, 2, 29);
    test_datetime(f.get(), "'2021-01-31' - 1month",  2020, 12,31);
    test_datetime(f.get(), "'2021-01-31' - 2month",  2020, 11, 30);
    test_datetime(f.get(), "'2021-01-31' - 24month", 2019, 1, 31);
    test_datetime(f.get(), "'2021-01-31' + 24month", 2023, 1, 31);
    test_datetime(f.get(), "'2021-01-31' + 22month", 2022, 11, 30);

    // 2020 was a leap year.
    test_datetime(f.get(), "'2021-02-28' -12month", 2020,2,29);
    // 2000 was a leap year %100=0 but %400=0 overrides.
    test_datetime(f.get(), "'2001-02-28' -12month", 2000, 2, 29);
    // 2100 is not a leap year since %100=0 and not overriden %400 != 0.
    test_datetime(f.get(), "'2000-02-29' +(12month * 100counter)", 2100,2,28);
}

void test_formulas_parsing_1()
{
    MeterInfo mi;
    mi.parse("testur", "ebzwmbe", "22992299", "");
    shared_ptr<Meter> meter = createMeter(&mi);

    vector<uchar> frame;
    hex2bin("5B445a149922992202378c20f6900f002c25Bc9e0000BBBBBBBBBBBBBBBB72992299225a140102f6003007102f2f040330f92a0004a9ff01ff24000004a9ff026a29000004a9ff03460600000dfd11063132333435362f2f2f2f2f2f", &frame);

    Telegram t;
    MeterKeys mk;
    t.parse(frame, &mk, true);

    vector<Address> id;
    bool match;
    meter->handleTelegram(t.about, frame, true, &id, &match, &t);

    unique_ptr<FormulaImplementation> f = unique_ptr<FormulaImplementation>(new FormulaImplementation());

    test_formula_value(f.get(), meter.get(),
                       "10 kwh + 100 kwh",
                       110,
                       Unit::KWH);

    test_formula_value(f.get(), meter.get(),
                       "current_power_consumption_phase1_kw + "
                       "current_power_consumption_phase2_kw + "
                       "current_power_consumption_phase3_kw + "
                       "100 kw",
                       100.21679,
                       Unit::KW);

    test_formula_tree(f.get(), meter.get(),
                      "5 c + 7 c + 10 c",
                      "<ADD <ADD <CONST 5 c[1c]Temperature> <CONST 7 c[1c]Temperature> > <CONST 10 c[1c]Temperature> >");

    test_formula_tree(f.get(), meter.get(),
                      "(5 c + 7 c) + 10 c",
                      "<ADD <ADD <CONST 5 c[1c]Temperature> <CONST 7 c[1c]Temperature> > <CONST 10 c[1c]Temperature> >");

    test_formula_tree(f.get(), meter.get(),
                      "5 c + (7 c + 10 c)",
                      "<ADD <CONST 5 c[1c]Temperature> <ADD <CONST 7 c[1c]Temperature> <CONST 10 c[1c]Temperature> > >");

    test_formula_tree(f.get(), meter.get(),
                      "sqrt(22 m * 22 m)",
                      "<SQRT <TIMES <CONST 22 m[1m]Length> <CONST 22 m[1m]Length> > >");

}

void test_formulas_parsing_2()
{
    MeterInfo mi;
    mi.parse("testur", "em24", "66666666", "");
    shared_ptr<Meter> meter = createMeter(&mi);

    vector<uchar> frame;
    hex2bin("35442D2C6666666633028D2070806A0520B4D378_0405F208000004FB82753F00000004853C0000000004FB82F53CCA01000001FD1722", &frame);

    Telegram t;
    MeterKeys mk;
    t.parse(frame, &mk, true);

    vector<Address> id;
    bool match;
    meter->handleTelegram(t.about, frame, true, &id, &match, &t);

    unique_ptr<FormulaImplementation> f = unique_ptr<FormulaImplementation>(new FormulaImplementation());

    test_formula_value(f.get(), meter.get(),
                       "total_energy_consumption_kwh + 18 kwh",
                       247,
                       Unit::KWH);
}

void test_formulas_multiply_constants()
{
    FormulaImplementation fi;

    test_formula_value(&fi, NULL, "100.5 counter * 22 kwh", 2211, Unit::KWH);
    test_formula_value(&fi, NULL, "5 kw * 10 h", 50, Unit::KWH);
    test_formula_value(&fi, NULL, "5000 v * 10 a * 700 h", 35000, Unit::KVAH);
}

void test_formulas_divide_constants()
{
    FormulaImplementation fi;

    test_formula_value(&fi, NULL, "22 kwh / 11 h", 2, Unit::KW);
}

void test_formulas_sqrt_constants()
{
    FormulaImplementation fi;

    test_formula_value(&fi, NULL, "sqrt(22 m * 22 m)", 22, Unit::M);
    test_formula_value(&fi, NULL, "sqrt((2 kwh * 2 kwh) + (3 kvarh * 3 kvarh))", 3.6055512754639891, Unit::KVAH);
}

void test_formulas_date_constants()
{
    FormulaImplementation fi;

//    test_formula_value(&fi, NULL, "2022-01-01 + 1 month", "2022-02-01");
}

void test_formulas_errors()
{
    {
        MeterInfo mi;
        mi.parse("testur", "em24", "66666666", "");

        auto meter = createMeter(&mi);
        auto formula = unique_ptr<FormulaImplementation>(new FormulaImplementation());

        test_formula_error(formula.get(), meter.get(),
                           "10 kwh + 20 kw", Unit::KWH,
                           "Cannot add [kwh|Energy|3.6×10⁶kgm²s⁻²] to [kw|Power|1000kgm²s⁻³]!\n"
                           "10 kwh + 20 kw\n"
                           "       ^~~~~\n");
    }

}

void test_formulas_dventries()
{
    FormulaImplementation fi;

    DVEntry dve;
    dve.storage_nr = 17;
    dve.tariff_nr = 3;
    dve.subunit_nr = 2;

    unique_ptr<FormulaImplementation> f = unique_ptr<FormulaImplementation>(new FormulaImplementation());

    string s = "(storage_counter - 12 counter) *  tariff_counter - subunit_counter";
    f->parse(NULL, s);

    f->setDVEntry(&dve);
    double v = f->calculate(Unit::COUNTER);

    if (v != 13.0)
    {
        printf("ERROR when calculating:\n%s\nExpected: %g but got: %g\n",
               s.c_str(),
               13.0,
               v);
    }

    dve.storage_nr = 18;
    dve.tariff_nr = 0;
    dve.subunit_nr = 0;

    s = "(storage_counter - 8counter) / 2counter";
    f->parse(NULL, s);

    f->setDVEntry(&dve);
    v = f->calculate(Unit::COUNTER);

    if (v != 5.0)
    {
        printf("ERROR when calculating:\n%s\nExpected: %g but got: %g\n",
               s.c_str(),
               5.0,
               v);
    }

}

void test_formulas_stringinterpolation()
{
    DVEntry dve;
    dve.storage_nr = 17;
    dve.tariff_nr = 3;
    dve.subunit_nr = 2;

    unique_ptr<StringInterpolator> f = unique_ptr<StringInterpolator>(new StringInterpolatorImplementation());

    string p = "history_{storage_counter-12counter}_value";
    f->parse(p);
    string s = f->apply(&dve);
    string e = "history_5_value";

    if (s != e)
    {
        printf("ERROR when interpolating\n%s\nExpected: %s but got: %s\n",
               p.c_str(),
               e.c_str(),
               s.c_str());
    }

    p = "{storage_counter}_{tariff_counter}_{2counter*subunit_counter}";
    f->parse(p);
    s = f->apply(&dve);
    e = "17_3_4";

    if (s != e)
    {
        printf("ERROR when interpolating\n%s\nExpected: %s but got: %s\n",
               p.c_str(),
               e.c_str(),
               s.c_str());
    }

}

void test_dynamic_loading()
{
    VIFRange vr = toVIFRange("Date");

    if (vr != VIFRange::Date)
    {
        printf("ERROR in dynamic loading got %s but expected %s!\n",
               toString(vr), toString(VIFRange::Date));
    }

    vr = toVIFRange("DateTime");

    if (vr != VIFRange::DateTime)
    {
        printf("ERROR in dynamic loading got %s but expected %s!\n",
               toString(vr), toString(VIFRange::DateTime));
    }
}
