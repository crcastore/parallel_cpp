#pragma once
#include <cstddef>
#include <vector>

// Non-owning view into a row-major block of doubles.
struct DataView
{
    const double *data{};
    std::size_t rows{};
    std::size_t cols{};
};

// Simple row-major matrix of doubles.
class Dataset
{
public:
    Dataset(std::size_t rows, std::size_t cols)
        : rows_(rows), cols_(cols), data_(rows * cols) {}

    std::size_t rows() const { return rows_; }
    std::size_t cols() const { return cols_; }
    double *data() { return data_.data(); }
    const double *data() const { return data_.data(); }

    double &at(std::size_t r, std::size_t c) { return data_[r * cols_ + c]; }
    double at(std::size_t r, std::size_t c) const { return data_[r * cols_ + c]; }
    const double *row_ptr(std::size_t r) const { return &data_[r * cols_]; }

private:
    std::size_t rows_{}, cols_{};
    std::vector<double> data_;
};
