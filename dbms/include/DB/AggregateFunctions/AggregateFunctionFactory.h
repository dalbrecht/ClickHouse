#pragma once

#include <DB/AggregateFunctions/IAggregateFunction.h>
#include <DB/DataTypes/IDataType.h>
#include <boost/iterator/transform_iterator.hpp>

namespace DB
{

/** Creates aggregate function by name.
  */
class AggregateFunctionFactory final
{
	friend class StorageSystemFunctions;

private:
	/// Not std::function, for lower object size and less indirection.
	using Creator = AggregateFunctionPtr(*)(const String & name, const DataTypes & argument_types);
	using AggregateFunctions = std::unordered_map<String, Creator>;

public:
	AggregateFunctionFactory();
	AggregateFunctionPtr get(const String & name, const DataTypes & argument_types, int recursion_level = 0) const;
	AggregateFunctionPtr tryGet(const String & name, const DataTypes & argument_types) const;
	bool isAggregateFunctionName(const String & name, int recursion_level = 0) const;

	/// For compatibility with SQL, it's possible to specify that certain aggregate function name is case insensitive.
	enum CaseSensitiveness
	{
		CaseSensitive,
		CaseInsensitive
	};

	/// Register aggregate function with its name.
	void registerFunction(const String & name, Creator creator, CaseSensitiveness case_sensitiveness = CaseSensitive);

	AggregateFunctionFactory(const AggregateFunctionFactory &) = delete;
	AggregateFunctionFactory & operator=(const AggregateFunctionFactory &) = delete;

private:
	AggregateFunctions aggregate_functions;

	/// Case insensitive aggregate functions will be additionally added here with lowercased name.
	AggregateFunctions case_insensitive_aggregate_functions;
};

}
