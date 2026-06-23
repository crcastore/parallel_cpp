#pragma once

#include <cstddef>
#include <vector>

class RowMajorDataset {
public:
    RowMajorDataset(std::size_t rows, std::size_t cols)
        : rows_(rows), cols_(cols), data_(rows * cols, 0.0) {}

    std::size_t rows() const { return rows_; }
    std::size_t cols() const { return cols_; }

    double& at(std::size_t row, std::size_t col) {
        return data_[row * cols_ + col];
    }

    double at(std::size_t row, std::size_t col) const {
        return data_[row * cols_ + col];
    }

    const double* row_ptr(std::size_t row) const {
        return &data_[row * cols_];
    }

private:
    std::size_t rows_;
    std::size_t cols_;
    std::vector<double> data_;
};
