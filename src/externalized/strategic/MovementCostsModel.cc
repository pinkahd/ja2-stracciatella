#include "MovementCostsModel.h"
#include "Debug.h"
#include "JsonObject.h"

#include <string_theory/string>

#include <map>


// helper functions
void readTraversibiltyIntoVector(const rapidjson::Value& jsonArray, IntIntVector& vec, const TraversibilityMap& mapping, size_t expectedRows, size_t expectedCols);
void readIntIntoVector(const rapidjson::Value& jsonArray, IntIntVector& vec, size_t expectedRows, size_t expectedCols);


MovementCostsModel::MovementCostsModel(IntIntVector traverseWE_, IntIntVector traverseNS_, IntIntVector traverseThrough_, IntIntVector travelRatings_)
	:traverseWE(traverseWE_), traverseNS(traverseNS_), traverseThrough(traverseThrough_), travelRatings(travelRatings_) {}

const uint8_t MovementCostsModel::getTraversibilityWestEast(const SGPSector& sSector) const
{
	Assert(sSector.x == 17 || sSector.IsValid());
	return traverseWE[sSector.y - 1][sSector.x - 1];
}

const uint8_t MovementCostsModel::getTraversibilityNorthSouth(const SGPSector& sSector) const
{
	Assert(sSector.y == 17 || sSector.IsValid());
	return traverseNS[sSector.y - 1][sSector.x - 1];
}

const uint8_t MovementCostsModel::getTraversibilityThrough(const SGPSector& sSector) const
{
	Assert(sSector.IsValid());
	return traverseThrough[sSector.y - 1][sSector.x - 1];
}

const uint8_t MovementCostsModel::getTravelRating(const SGPSector& sSector) const
{
	Assert(sSector.IsValid());
	return travelRatings[sSector.y - 1][sSector.x - 1];
}

MovementCostsModel* MovementCostsModel::deserialize(const rapidjson::Document& root, const TraversibilityMap& mapToEnum)
{
	Assert(root.HasMember("traverseWE") && root["traverseWE"].IsArray());
	IntIntVector traverseWE_;
	readTraversibiltyIntoVector(root["traverseWE"], traverseWE_, mapToEnum, 16, 17);

	Assert(root.HasMember("traverseNS") && root["traverseNS"].IsArray());
	IntIntVector traverseNS_;
	readTraversibiltyIntoVector(root["traverseNS"], traverseNS_, mapToEnum, 17, 16);

	Assert(root.HasMember("traverseThrough") && root["traverseThrough"].IsArray());
	IntIntVector traverseThrough_;
	readTraversibiltyIntoVector(root["traverseThrough"], traverseThrough_, mapToEnum, 16, 16);

	Assert(root.HasMember("travelRatings") && root["travelRatings"].IsArray());
	IntIntVector travelRatings_;
	readIntIntoVector(root["travelRatings"], travelRatings_, 16, 16);

	return new MovementCostsModel(
		traverseWE_,
		traverseNS_,
		traverseThrough_,
		travelRatings_
	);
}

void readTraversibiltyIntoVector(const rapidjson::Value& jsonArray, IntIntVector& vec, const TraversibilityMap& mapping, size_t expectedRows, size_t expectedCols)
{
	for (auto& r : jsonArray.GetArray())
	{
		auto row = std::vector<uint8_t>();
		for (auto& c : r.GetArray())
		{
			row.push_back(mapping.at(ST::string(c.GetString())));
		}

		if (row.size() != expectedCols) {
			SLOGE("num of columns is different from expected");
			throw std::runtime_error("num of columns is different from expected");
		}
		vec.push_back(row);
	}
	if (vec.size() != expectedRows) {
		SLOGE("num of rows is different from expected");
		throw std::runtime_error("num of rows is different from expected");
	}
}

void readIntIntoVector(const rapidjson::Value& jsonArray, IntIntVector& vec, size_t expectedRows, size_t expectedCols)
{
	for (auto& r : jsonArray.GetArray())
	{
		auto row = std::vector<uint8_t>();
		for (auto& c : r.GetArray())
		{
			row.push_back(c.GetInt());
		}

		if (row.size() != expectedCols) {
			SLOGE("num of columns is different from expected");
			throw std::runtime_error("num of columns is different from expected");
		}
		vec.push_back(row);
	}
	if (vec.size() != expectedRows) {
		SLOGE("num of rows is different from expected");
		throw std::runtime_error("num of row is different from expected");
	}
}
