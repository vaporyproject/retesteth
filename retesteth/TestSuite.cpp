/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
 */
/** @file
 * Base functions for all test suites
 */

#include <boost/test/unit_test.hpp>
#include <json/reader.h>
#include <json/value.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/Log.h>
#include <libdevcore/SHA3.h>
#include <retesteth/DataObject.h>
#include <retesteth/EthChecks.h>
#include <retesteth/ExitHandler.h>
#include <retesteth/Options.h>
#include <retesteth/RPCSession.h>
#include <retesteth/TestHelper.h>
#include <retesteth/TestOutputHelper.h>
#include <retesteth/TestSuite.h>
#include <string>
#include <thread>


using namespace std;
using namespace dev;
namespace fs = boost::filesystem;

//Helper functions for test proccessing
namespace {
struct TestFileData
{
    DataObject data;
    h256 hash;
};

TestFileData readTestFile(fs::path const& _testFileName)
{
    TestFileData testData;
    Json::Value v = readJson(_testFileName);
    if (_testFileName.extension() == ".json")
        testData.data = test::convertJsonCPPtoData(v);
    // else if (_testFileName.extension() == ".yml")
    //    testData.data = test::parseYamlToJson(s);
    else
        BOOST_ERROR("Unknow test format!" + test::TestOutputHelper::get().testFile().string());

    Json::FastWriter fastWriter;
    std::string output = fastWriter.write(v);
    output = output.substr(0, output.size() - 1);
    testData.hash = sha3(output);
    return testData;
}

void removeComments(test::DataObject& _obj)
{
    if (_obj.type() == test::DataType::Object)
	{
		list<string> removeList;
        for (auto& i: _obj.getSubObjectsUnsafe())
		{
            if (i.getKey().substr(0, 2) == "//")
			{
                removeList.push_back(i.getKey());
				continue;
			}
            removeComments(i);
		}
        for (auto const& i: removeList)
            _obj.removeKey(i);
	}
    else if (_obj.type() == test::DataType::Array)
	{
        for (auto& i: _obj.getSubObjectsUnsafe())
			removeComments(i);
    }
}

void addClientInfo(test::DataObject& _v, fs::path const& _testSource, h256 const& _testSourceHash)
{
  RPCSession &session = RPCSession::instance(TestOutputHelper::getThreadID());
  for (auto& o : _v.getSubObjectsUnsafe())
  {
      string comment;
      test::DataObject clientinfo;
      if (o.count("_info"))
      {
          test::DataObject const& existingInfo = o.at("_info");
          if (existingInfo.count("comment"))
              comment = existingInfo.at("comment").asString();
      }

      clientinfo.setKey("_info");
      clientinfo["comment"] = comment;
      clientinfo["filling-rpc-server"] = session.web3_clientVersion();
      clientinfo["filling-tool-version"] = test::prepareVersionString();
      clientinfo["lllcversion"] = test::prepareLLLCVersionString();
      clientinfo["source"] = _testSource.string();
      clientinfo["sourceHash"] = toString(_testSourceHash);

      o["_info"].replace(clientinfo);
      o.setKeyPos("_info", 0);
    }
}

void checkFillerHash(fs::path const& _compiledTest, fs::path const& _sourceTest)
{
    test::DataObject v = test::convertJsonCPPtoData(test::readJson(_compiledTest));
    TestFileData fillerData = readTestFile(_sourceTest);
    for (auto const& i: v.getSubObjects())
	{
        // use eth object _info section class here !!!!!
        ETH_REQUIRE_MESSAGE(i.type() == test::DataType::Object, i.getKey() + " should contain an object under a test name.");
        ETH_REQUIRE_MESSAGE(i.count("_info") > 0, "_info section not set! " + _compiledTest.string());
        test::DataObject const& info = i.at("_info");
        ETH_REQUIRE_MESSAGE(info.count("sourceHash") > 0, "sourceHash not found in " + _compiledTest.string() + " in " + i.getKey());
        h256 const sourceHash = h256(info.at("sourceHash").asString());
        ETH_CHECK_MESSAGE(sourceHash == fillerData.hash,
            "Test " + _compiledTest.string() + " in " + i.getKey() +
                " is outdated. Filler hash is different! ( '" + sourceHash.hex().substr(0, 4) +
                "' != '" + fillerData.hash.hex().substr(0, 4) + "') ");
    }
}

void joinThreads(vector<thread>& _threadVector, bool _all)
{
    if (_all)
    {
        for (auto& th : _threadVector)
        {
            string id = toString(th.get_id());
            th.join();
            RPCSession::sessionEnd(id, RPCSession::SessionStatus::Available);
        }
        _threadVector.clear();
        if (ExitHandler::shouldExit())
            ExitHandler::couldExit();
        return;
    }

    bool finished = false;
    while (!finished)
    {
        for (vector<thread>::iterator it = _threadVector.begin(); it != _threadVector.end(); it++)
        {
            finished =
                (RPCSession::sessionStatus(toString((*it).get_id())) == RPCSession::HasFinished);
            if (finished)
            {
                string id = toString((*it).get_id());
                (*it).join();
                RPCSession::sessionEnd(id, RPCSession::SessionStatus::Available);
                _threadVector.erase(it);
                return;
            }
        }
    }
}
}

namespace test
{
string const c_fillerPostf = "Filler";
string const c_copierPostf = "Copier";

void TestSuite::runTestWithoutFiller(boost::filesystem::path const& _file) const
{
    // Allow to execute a custom test .json file on any test suite
    auto& testOutput = test::TestOutputHelper::get();
    testOutput.initTest(1);
    executeFile(_file);
    testOutput.finishTest();
}

string TestSuite::checkFillerExistance(string const& _testFolder) const
{
    string filter = test::Options::get().singleTestName.empty() ?
                        string() :
                        test::Options::get().singleTestName;
    std::cout << "Filter: " << filter << std::endl;
    vector<fs::path> const compiledFiles =
        test::getFiles(getFullPath(_testFolder), {".json", ".yml"}, filter);
    for (auto const& file : compiledFiles)
    {
        fs::path const expectedFillerName =
            getFullPathFiller(_testFolder) /
            fs::path(file.stem().string() + c_fillerPostf + ".json");
        fs::path const expectedFillerName2 =
            getFullPathFiller(_testFolder) /
            fs::path(file.stem().string() + c_fillerPostf + ".yml");
        fs::path const expectedCopierName =
            getFullPathFiller(_testFolder) /
            fs::path(file.stem().string() + c_copierPostf + ".json");
        ETH_REQUIRE_MESSAGE(fs::exists(expectedFillerName) || fs::exists(expectedFillerName2) || fs::exists(expectedCopierName), "Compiled test folder contains test without Filler: " + file.filename().string());
        ETH_REQUIRE_MESSAGE(!(fs::exists(expectedFillerName) && fs::exists(expectedFillerName2) && fs::exists(expectedCopierName)), "Src test could either be Filler.json, Filler.yml or Copier.json: " + file.filename().string());

        // Check that filled tests created from actual fillers depenging on a test type
        if (fs::exists(expectedFillerName))
        {
            if (Options::get().filltests == false)  // If we are filling the test it is probably
                                                    // outdated/being updated. no need to check.
                checkFillerHash(file, expectedFillerName);
            if (!filter.empty())
                return filter + c_fillerPostf;
        }
        if (fs::exists(expectedFillerName2))
        {
            if (Options::get().filltests == false)
                checkFillerHash(file, expectedFillerName2);
            if (!filter.empty())
                return filter + c_fillerPostf;
        }
        if (fs::exists(expectedCopierName))
        {
            if (Options::get().filltests == false)
                checkFillerHash(file, expectedCopierName);
            if (!filter.empty())
                return filter + c_copierPostf;
        }
    }
    return string("Error selecting filter!");
}

void TestSuite::runAllTestsInFolder(string const& _testFolder) const
{
    if (ExitHandler::shouldExit())
        return;

    // check that destination folder test files has according Filler file in src folder
    string const filter = checkFillerExistance(_testFolder);

    // run all tests
    vector<fs::path> const files =
        test::getFiles(getFullPathFiller(_testFolder), {".json", ".yml"}, filter);

    // repeat this part for all connected clients
    auto thisPart = [this, &files, &_testFolder]() {
        auto& testOutput = test::TestOutputHelper::get();
        vector<thread> threadVector;
        testOutput.initTest(files.size());
        for (auto const& file : files)
        {
            if (ExitHandler::shouldExit())
                break;
            testOutput.showProgress();
            if (threadVector.size() == Options::get().threadCount)
                joinThreads(threadVector, false);
            thread testThread(&TestSuite::executeTest, this, _testFolder, file);
            threadVector.push_back(std::move(testThread));
        }
        joinThreads(threadVector, true);
        testOutput.finishTest();
    };
    runFunctionForAllClients(thisPart);
}


void TestSuite::runFunctionForAllClients(std::function<void()> _func)
{
    for (auto const& config : Options::getDynamicOptions().getClientConfigs())
    {
        Options::getDynamicOptions().setCurrentConfig(config);
        std::cout << "Running tests for config '" << config.getName() << "' " << config.getId()
                  << std::endl;
        _func();
        RPCSession::clear();
    }
}

fs::path TestSuite::getFullPathFiller(string const& _testFolder) const
{
	return test::getTestPath() / "src" / suiteFillerFolder() / _testFolder;
}

fs::path TestSuite::getFullPath(string const& _testFolder) const
{
	return test::getTestPath() / suiteFolder() / _testFolder;
}

void TestSuite::executeTest(string const& _testFolder, fs::path const& _testFileName) const
{
    RPCSession::sessionStart(TestOutputHelper::getThreadID());
    fs::path const boostRelativeTestPath = fs::relative(_testFileName, getTestPath());
    string testname = _testFileName.stem().string();
    bool isCopySource = false;
    if (testname.rfind(c_fillerPostf) != string::npos)
        testname = testname.substr(0, testname.rfind("Filler"));
    else if (testname.rfind(c_copierPostf) != string::npos)
    {
        testname = testname.substr(0, testname.rfind(c_copierPostf));
        isCopySource = true;
    }
    else
        ETH_REQUIRE_MESSAGE(false, "Incorrect file suffix in the filler folder! " + _testFileName.string());

    // Filename of the test that would be generated
    fs::path const boostTestPath = getFullPath(_testFolder) / fs::path(testname + ".json");

    TestSuiteOptions opt;
    if (Options::get().filltests)
    {
        if (isCopySource)
        {
            clog << "Copying " << _testFileName.string() << "\n";
            clog << " TO " << boostTestPath.string() << "\n";
            assert(_testFileName.string() != boostTestPath.string());
            TestOutputHelper::get().showProgress();
            test::copyFile(_testFileName, boostTestPath);
            ETH_REQUIRE_MESSAGE(boost::filesystem::exists(boostTestPath.string()), "Error when copying the test file!");

            // Update _info and build information of the copied test
            /*Json::Value v;
            string const s = asString(dev::contents(boostTestPath));
            json_spirit::read_string(s, v);
            addClientInfo(v, boostRelativeTestPath, sha3(dev::contents(_testFileName)));
            writeFile(boostTestPath, asBytes(json_spirit::write_string(v, true)));*/
        }
        else
        {
            TestFileData testData = readTestFile(_testFileName);
            removeComments(testData.data);
            opt.doFilling = true;

            try
            {
                DataObject output = doTests(testData.data, opt);
                if (!opt.wasErrors)
                {
                    // Add client info for all of the tests in output
                    addClientInfo(output, boostRelativeTestPath, testData.hash);
                    writeFile(boostTestPath, asBytes(output.asJson()));
                }
            }
            catch (std::exception const& _ex)
            {
                ETH_ERROR("ERROR OCCURED: " + string(_ex.what()));
                RPCSession::sessionEnd(TestOutputHelper::getThreadID(), RPCSession::SessionStatus::HasFinished);
            }
        }
    }

    if (!opt.wasErrors)
    {
        // Test is generated. Now run it and check that there should be no errors
        if ((Options::get().singleTest && Options::get().singleTestName == testname) || !Options::get().singleTest)
            cnote << "TEST " << testname + ":";

        try
        {
            executeFile(boostTestPath);
        }
        catch (std::exception const& _ex)
        {
            ETH_ERROR("ERROR OCCURED: " + string(_ex.what()));
            RPCSession::sessionEnd(TestOutputHelper::getThreadID(), RPCSession::SessionStatus::HasFinished);
        }
    }
    RPCSession::sessionEnd(TestOutputHelper::getThreadID(), RPCSession::SessionStatus::HasFinished);
}

void TestSuite::executeFile(boost::filesystem::path const& _file) const
{
    TestSuiteOptions opt;
    doTests(test::convertJsonCPPtoData(readJson(_file)), opt);
}

}

