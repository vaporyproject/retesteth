#pragma once
#include <devcore/Common.h>
#include <testeth/ethObjects/object.h>
#include <testeth/TestOutputHelper.h>
#include <testeth/TestHelper.h>

using namespace dev;
namespace  test {
	enum CompareResult
	{
		Success,
		AccountShouldNotExist,
		MissingExpectedAccount,
		IncorrectBalance,
		IncorrectNonce,
		IncorrectCode,
		IncorrectStorage,
		None
	};

	class account : public object
	{
		public:
		account(DataObject const& _account):
			object(_account)
		{
            requireJsonFields(_account, "account " + _account.getKey(), {
                {"balance", {DataType::String} },
                {"code", {DataType::String} },
                {"nonce", {DataType::String} },
                {"storage", {DataType::Object} }
            });

			// Compile the code
			m_data["code"] = test::replaceCode(m_data.at("code").asString());

            // Make all fields hex
            m_data.setKey(makeHexAddress(m_data.getKey()));
            makeAllFieldsHex(m_data);
		}
    };

    class expectAccount: public object
    {
        public:
        expectAccount(DataObject const& _account):
            object(_account)
        {
            m_shouldNotExist = _account.count("shouldnotexist");
            m_hasBalance = _account.count("balance");
            m_hasNonce = _account.count("nonce");
            m_hasCode = _account.count("code");
            m_hasStorage = _account.count("storage");

            // Make all fields hex
            m_data.setKey(account::makeHexAddress(m_data.getKey()));
            makeAllFieldsHex(m_data);
        }

        bool shouldNotExist() const { return m_shouldNotExist; }
        bool hasBalance() const { return m_hasBalance; }
        bool hasNonce() const { return m_hasNonce; }
        bool hasCode() const { return m_hasCode; }
        bool hasStorage() const { return m_hasStorage; }
        std::string const& address() const { return getData().getKey(); }
		CompareResult compareStorage(DataObject const& _storage) const
        {
			CompareResult result = CompareResult::Success;
			auto checkMessage = [&result](bool _flag, CompareResult _type, std::string const& _error) -> void
			{
				BOOST_CHECK_MESSAGE(_flag, _error);
				if (!_flag)
					result = _type;
			};

			BOOST_REQUIRE(_storage.type() == DataType::Object);
            BOOST_REQUIRE(hasStorage());

			DataObject const& expectStorage = m_data.at("storage");
			for (auto const& element: expectStorage.getSubObjects())
			{
				checkMessage(_storage.count(element.getKey()),
				   CompareResult::IncorrectStorage,
                   TestOutputHelper::get().testName() + " '" + address() + "' expected storage key: '"
                    + element.getKey() + "' to be set!");

				if (result != CompareResult::Success)
					return result;

                std::string valueInStorage = _storage.at(element.getKey()).asString();
				checkMessage(valueInStorage == element.asString(),
				   CompareResult::IncorrectStorage,
				   TestOutputHelper::get().testName() + " Check State: " + address()
				   + ": incorrect storage [" + element.getKey() + "] = " + valueInStorage
				   + ", expected [" + element.getKey() + "] = " + element.asString());
            }
			checkMessage(expectStorage.getSubObjects().size() == _storage.getSubObjects().size(),
				CompareResult::IncorrectStorage,
				 TestOutputHelper::get().testName() + address() + " storage has more storage records then expected!");
			return result;
        }

        private:
        bool m_shouldNotExist;
        bool m_hasBalance;
        bool m_hasNonce;
        bool m_hasCode;
        bool m_hasStorage;
    };
}
