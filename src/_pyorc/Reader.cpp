#include <pybind11/stl.h>

#include "Column.h"
#include "PyORCStream.h"
#include "Reader.h"

py::object
ORCIterator::next()
{
    while (true) {
        if (batchItem == 0) {
            if (!rowReader->next(*batch)) {
                throw py::stop_iteration();
            }
            converter->reset(*batch);
        }
        if (batchItem < batch->numElements) {
            py::object val = converter->toPython(batchItem);
            ++batchItem;
            ++currentRow;
            return val;
        } else {
            batchItem = 0;
        }
    }
}

py::object
ORCIterator::read(int64_t num)
{
    int64_t i = 0;
    py::list res;
    if (num < -1) {
        throw py::value_error("Read length must be positive or -1");
    }
    try {
        while (true) {
            if (num != -1 && i == num) {
                return res;
            }
            res.append(this->next());
            ++i;
        }
    } catch (py::stop_iteration&) {
        return res;
    }
}

uint64_t
ORCStream::seek(int64_t row, uint16_t whence)
{
    uint64_t start = 0;
    switch (whence) {
        case 0:
            start = firstRowOfStripe;
            if (row < 0) {
                throw py::value_error("Invalid value for row");
            }
            break;
        case 1:
            start = currentRow + firstRowOfStripe;
            break;
        case 2:
            start = this->len() + firstRowOfStripe;
            break;
        default:
            throw py::value_error("Invalid value for whence");
            break;
    }
    rowReader->seekToRow(start + row);
    batchItem = 0;
    currentRow = rowReader->getRowNumber() - firstRowOfStripe;
    return currentRow;
}

py::object
ORCStream::convertTimestampMillis(int64_t millisec) const
{
    py::object idx(py::int_(static_cast<int>(orc::TIMESTAMP)));
    py::object from_orc = this->getConverters()[idx].attr("from_orc");
    int64_t seconds = millisec / 1000;
    int64_t nanosecs = std::abs(millisec % 1000) * 1000 * 1000;
    return from_orc(seconds, nanosecs);
}

py::object
ORCStream::buildStatistics(const orc::Type* type,
                           const orc::ColumnStatistics* stats) const
{
    py::dict result;
    py::object enumKind = py::module::import("pyorc.enums").attr("TypeKind");
    int64_t typeKind = static_cast<int64_t>(type->getKind());
    result["kind"] = enumKind(typeKind);
    result["has_null"] = py::cast(stats->hasNull());
    result["number_of_values"] = py::cast(stats->getNumberOfValues());
    switch (typeKind) {
        case orc::BOOLEAN: {
            auto* boolStat = dynamic_cast<const orc::BooleanColumnStatistics*>(stats);
            if (boolStat->hasCount()) {
                result["false_count"] = py::cast(boolStat->getFalseCount());
                result["true_count"] = py::cast(boolStat->getTrueCount());
            }
            return result;
        }
        case orc::BYTE:
        case orc::INT:
        case orc::LONG:
        case orc::SHORT: {
            auto* intStat = dynamic_cast<const orc::IntegerColumnStatistics*>(stats);
            if (intStat->hasMinimum()) {
                result["minimum"] = py::cast(intStat->getMinimum());
            }
            if (intStat->hasMaximum()) {
                result["maximum"] = py::cast(intStat->getMaximum());
            }
            if (intStat->hasSum()) {
                result["sum"] = py::cast(intStat->getSum());
            }
            return result;
        }
        case orc::STRUCT:
        case orc::MAP:
        case orc::LIST:
        case orc::UNION:
            return result;
        case orc::FLOAT:
        case orc::DOUBLE: {
            auto* doubleStat = dynamic_cast<const orc::DoubleColumnStatistics*>(stats);
            if (doubleStat->hasMinimum()) {
                result["minimum"] = py::cast(doubleStat->getMinimum());
            }
            if (doubleStat->hasMaximum()) {
                result["maximum"] = py::cast(doubleStat->getMaximum());
            }
            if (doubleStat->hasSum()) {
                result["sum"] = py::cast(doubleStat->getSum());
            }
            return result;
        }
        case orc::BINARY: {
            auto* binaryStat = dynamic_cast<const orc::BinaryColumnStatistics*>(stats);
            if (binaryStat->hasTotalLength()) {
                result["total_length"] = py::cast(binaryStat->getTotalLength());
            }
            return result;
        }
        case orc::STRING:
        case orc::CHAR:
        case orc::VARCHAR: {
            auto* strStat = dynamic_cast<const orc::StringColumnStatistics*>(stats);
            if (strStat->hasMinimum()) {
                result["minimum"] = py::cast(strStat->getMinimum());
            }
            if (strStat->hasMaximum()) {
                result["maximum"] = py::cast(strStat->getMaximum());
            }
            if (strStat->hasTotalLength()) {
                result["total_length"] = py::cast(strStat->getTotalLength());
            }
            return result;
        }
        case orc::DATE: {
            auto* dateStat = dynamic_cast<const orc::DateColumnStatistics*>(stats);
            py::object idx(py::int_(static_cast<int>(orc::DATE)));
            py::object from_orc = this->getConverters()[idx].attr("from_orc");
            if (dateStat->hasMinimum()) {
                result["minimum"] = from_orc(dateStat->getMinimum());
            }
            if (dateStat->hasMaximum()) {
                result["maximum"] = from_orc(dateStat->getMaximum());
            }
            return result;
        }
        case orc::TIMESTAMP: {
            auto* timeStat = dynamic_cast<const orc::TimestampColumnStatistics*>(stats);
            if (timeStat->hasMinimum()) {
                result["minimum"] = convertTimestampMillis(timeStat->getMinimum());
            }
            if (timeStat->hasMaximum()) {
                result["maximum"] = convertTimestampMillis(timeStat->getMaximum());
            }
            if (timeStat->hasLowerBound()) {
                result["lower_bound"] =
                  convertTimestampMillis(timeStat->getLowerBound());
            }
            if (timeStat->hasUpperBound()) {
                result["upper_bound"] =
                  convertTimestampMillis(timeStat->getUpperBound());
            }
            return result;
        }
        case orc::DECIMAL: {
            auto* decStat = dynamic_cast<const orc::DecimalColumnStatistics*>(stats);
            py::object idx(py::int_(static_cast<int>(orc::DECIMAL)));
            py::object from_orc = this->getConverters()[idx].attr("from_orc");
            if (decStat->hasMinimum()) {
                result["minimum"] = from_orc(decStat->getMinimum().toString());
            }
            if (decStat->hasMaximum()) {
                result["maximum"] = from_orc(decStat->getMaximum().toString());
            }
            if (decStat->hasSum()) {
                result["sum"] = from_orc(decStat->getSum().toString());
            }
            return result;
        }
        default:
            return result;
    }
}

Reader::Reader(py::object fileo,
               uint64_t batch_size,
               std::list<uint64_t> col_indices,
               std::list<std::string> col_names,
               unsigned int struct_repr,
               py::object conv)
{
    orc::ReaderOptions readerOpts;
    batchItem = 0;
    currentRow = 0;
    firstRowOfStripe = 0;
    structKind = struct_repr;
    if (!col_indices.empty() && !col_names.empty()) {
        throw py::value_error(
          "Either col_indices or col_names can be set to select columns");
    }
    if (!col_indices.empty()) {
        rowReaderOpts = rowReaderOpts.include(col_indices);
    }
    if (!col_names.empty()) {
        rowReaderOpts = rowReaderOpts.include(col_names);
    }
    if (conv.is(py::none())) {
        py::dict defaultConv =
          py::module::import("pyorc.converters").attr("DEFAULT_CONVERTERS");
        converters = py::dict(defaultConv);
    } else {
        converters = conv;
    }
    reader = createReader(
      std::unique_ptr<orc::InputStream>(new PyORCInputStream(fileo)), readerOpts);
    typeDesc = std::make_unique<TypeDescription>(reader->getType());
    try {
        batchSize = batch_size;
        rowReader = reader->createRowReader(rowReaderOpts);
        batch = rowReader->createRowBatch(batchSize);
        converter =
          createConverter(&rowReader->getSelectedType(), structKind, converters);
    } catch (orc::ParseError& err) {
        throw py::value_error(err.what());
    }
}

Column
Reader::getItem(uint64_t num)
{
    uint32_t colIdx = static_cast<uint32_t>(num);
    std::map<uint32_t, orc::BloomFilterIndex> bloomFilters;
    std::set<uint32_t> singleSet = { colIdx };
    for (uint64_t i = 0; i < reader->getNumberOfStripes(); ++i) {
        std::map<uint32_t, orc::BloomFilterIndex> part =
          getORCReader().getBloomFilters(static_cast<uint32_t>(i), singleSet);
        if (!part.empty()) {
            bloomFilters[colIdx].entries.reserve(part[colIdx].entries.size());
            bloomFilters[colIdx].entries.insert(bloomFilters[colIdx].entries.end(),
                                                part[colIdx].entries.begin(),
                                                part[colIdx].entries.end());
        }
    }
    return Column(*this, num, bloomFilters);
}

uint64_t
Reader::len() const
{
    return reader->getNumberOfRows();
}

uint64_t
Reader::numberOfStripes() const
{
    return reader->getNumberOfStripes();
}

Stripe
Reader::readStripe(uint64_t idx)
{
    if (idx >= reader->getNumberOfStripes()) {
        throw py::index_error("stripe index out of range");
    }
    return Stripe(*this, idx, reader->getStripe(idx));
}

TypeDescription&
Reader::schema()
{
    return *typeDesc;
}

py::tuple
Reader::createStatistics(const orc::Type* type, uint64_t columnIndex) const
{
    py::tuple result = py::tuple(1);
    std::unique_ptr<orc::ColumnStatistics> stats =
      reader->getColumnStatistics(columnIndex);
    result[0] = this->buildStatistics(type, stats.get());
    return result;
}

Stripe::Stripe(const Reader& reader_,
               uint64_t idx,
               std::unique_ptr<orc::StripeInformation> stripe)
  : reader(reader_)
{
    batchItem = 0;
    currentRow = 0;
    stripeIndex = idx;
    stripeInfo = std::move(stripe);
    batchSize = reader.getBatchSize();
    structKind = reader.getStructKind();
    converters = reader.getConverters();
    rowReaderOpts = reader.getRowReaderOptions();
    rowReaderOpts =
      rowReaderOpts.range(stripeInfo->getOffset(), stripeInfo->getLength());
    rowReader = getORCReader().createRowReader(rowReaderOpts);
    batch = rowReader->createRowBatch(reader.getBatchSize());
    converter = createConverter(&rowReader->getSelectedType(), structKind, converters);
    firstRowOfStripe = rowReader->getRowNumber() + 1;
}

py::object
Stripe::bloomFilterColumns()
{
    int64_t idx = 0;
    std::set<uint32_t> empty = {};
    std::map<uint32_t, orc::BloomFilterIndex> bfCols =
      getORCReader().getBloomFilters(stripeIndex, empty);
    py::tuple result(bfCols.size());
    for (auto const& col : bfCols) {
        result[idx] = py::cast(col.first);
        ++idx;
    }
    return result;
}

Column
Stripe::getItem(uint64_t num)
{
    std::set<uint32_t> singleSet = { static_cast<uint32_t>(num) };
    std::map<uint32_t, orc::BloomFilterIndex> bloomFilters =
      getORCReader().getBloomFilters(static_cast<uint32_t>(stripeIndex), singleSet);
    return Column(*this, num, bloomFilters);
}

uint64_t
Stripe::len() const
{
    return stripeInfo->getNumberOfRows();
}

uint64_t
Stripe::length() const
{
    return stripeInfo->getLength();
}

uint64_t
Stripe::offset() const
{
    return stripeInfo->getOffset();
}

std::string
Stripe::writerTimezone()
{
    return stripeInfo->getWriterTimezone();
}

py::tuple
Stripe::createStatistics(const orc::Type* type, uint64_t columnIndex) const
{
    std::unique_ptr<orc::StripeStatistics> stripeStats =
      reader.getORCReader().getStripeStatistics(stripeIndex);
    py::tuple result = py::tuple(stripeStats->getNumberOfRowIndexStats(columnIndex));
    for (uint32_t i = 0; i < stripeStats->getNumberOfRowIndexStats(columnIndex); ++i) {
        const orc::ColumnStatistics* stats =
          stripeStats->getRowIndexStatistics(columnIndex, i);
        result[i] = this->buildStatistics(type, stats);
    }
    return result;
}
