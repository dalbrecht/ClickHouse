#define BOOST_TEST_MODULE StrongTypedef

#include <common/strong_typedef.h>
#include <set>
#include <unordered_set>
#include <memory>
#include <type_traits>

#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(StrongTypedefSuite)

BOOST_AUTO_TEST_CASE(TypedefsOfTheSameType)
{
	/// check that strong typedefs of same type differ
	STRONG_TYPEDEF(int, Int);
	STRONG_TYPEDEF(int, AnotherInt);

	BOOST_CHECK(!(std::is_same<Int, AnotherInt>::value));
}

BOOST_AUTO_TEST_CASE(Map)
{
	STRONG_TYPEDEF(int, Int);

	/// check that this code compiles
	std::set<Int> int_set;
	int_set.insert(Int(1));
	std::unordered_set<Int> int_unorderd_set;
	int_unorderd_set.insert(Int(2));
}

BOOST_AUTO_TEST_CASE(CopyAndMoveCtor)
{
	STRONG_TYPEDEF(int, Int);
	Int a(1);
	Int b(2);
	a = b;
	BOOST_CHECK_EQUAL(a.t, 2);

	STRONG_TYPEDEF(std::unique_ptr<int>, IntPtr);
	{
		IntPtr ptr;
		ptr = IntPtr(std::make_unique<int>(3));
		BOOST_CHECK_EQUAL(*ptr.t, 3);
	}

	{
		IntPtr ptr(std::make_unique<int>(3));
		BOOST_CHECK_EQUAL(*ptr.t, 3);
	}
}

BOOST_AUTO_TEST_CASE(NoDefaultCtor)
{
	struct NoDefaultCtor
	{
		NoDefaultCtor(int i) {}
	};

	STRONG_TYPEDEF(NoDefaultCtor, MyStruct);
	MyStruct m(1);
}

BOOST_AUTO_TEST_SUITE_END()