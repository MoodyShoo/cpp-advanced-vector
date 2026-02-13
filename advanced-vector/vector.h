#pragma once
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <utility>
template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity) : buffer_(Allocate(capacity)), capacity_(capacity) {}

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;

    RawMemory(RawMemory&& other) noexcept {
        buffer_ = other.buffer_;
        capacity_ = other.capacity_;
        other.buffer_ = nullptr;
        other.capacity_ = 0;
    }

    RawMemory& operator=(RawMemory&& rhs) noexcept {
        if (this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }

    ~RawMemory() { Deallocate(buffer_); }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept { return const_cast<RawMemory&>(*this) + offset; }

    const T& operator[](size_t index) const noexcept { return const_cast<RawMemory&>(*this)[index]; }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept { return buffer_; }

    T* GetAddress() noexcept { return buffer_; }

    size_t Capacity() const { return capacity_; }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) { return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr; }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept { operator delete(buf); }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;
    Vector() = default;

    explicit Vector(size_t size) : data_(size), size_(size) {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector& other) : data_(other.size_), size_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector tmp(rhs);
                Swap(tmp);
            } else {
                if (size_ > rhs.size_) {
                    std::copy_n(rhs.data_.GetAddress(), rhs.size_, data_.GetAddress());
                    std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
                } else {
                    std::copy_n(rhs.data_.GetAddress(), size_, data_.GetAddress());
                    std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.size_ - size_,
                                              data_.GetAddress() + size_);
                }
                size_ = rhs.size_;
            }
        }
        return *this;
    }

    Vector(Vector&& other) noexcept : data_(std::move(other.data_)), size_(std::move(other.size_)) { other.size_ = 0; }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            data_ = std::move(rhs.data_);
            size_ = std::move(rhs.size_);
            rhs.size_ = 0;
        }

        return *this;
    }

    void Swap(Vector& other) noexcept {
        std::swap(other.data_, data_);
        std::swap(other.size_, size_);
    }

    ~Vector() { std::destroy_n(data_.GetAddress(), size_); }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }

        RawMemory<T> new_data{new_capacity};
        SwapData(new_data);
    }

    void Resize(size_t new_size) {
        if (new_size < size_) {
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
        } else {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
        }
        size_ = new_size;
    }

    void PushBack(const T& value) { EmplaceBack(value); }

    void PushBack(T&& value) { EmplaceBack(std::move(value)); }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        return *Emplace(end(), std::forward<Args>(args)...);
    }

    void PopBack() {
        std::destroy_at(data_.GetAddress() + size_ - 1);
        --size_;
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        size_t drift = pos - begin();
        if (size_ == Capacity()) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            new (new_data + drift) T(std::forward<Args>(args)...);
            ShiftSwap(new_data, drift);
        } else {
            if (pos != end()) {
                T tmp(std::forward<Args>(args)...);
                new (data_ + size_) T(std::move(*std::prev(end())));
                std::move_backward(data_ + drift, end() - 1, end());
                data_[drift] = std::move(tmp);
            } else {
                new (data_ + size_) T(std::forward<Args>(args)...);
            }
        }
        ++size_;
        return &data_[drift];
    }

    iterator Erase(const_iterator pos) {
        size_t drift = pos - begin();
        std::move(begin() + drift + 1, end(), begin() + drift);
        PopBack();
        return begin() + drift;
    }

    iterator Insert(const_iterator pos, const T& value) { return Emplace(pos, value); }

    iterator Insert(const_iterator pos, T&& value) { return Emplace(pos, std::move(value)); }

    size_t Size() const noexcept { return size_; }

    size_t Capacity() const noexcept { return data_.Capacity(); }

    const T& operator[](size_t index) const noexcept { return const_cast<Vector&>(*this)[index]; }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    // Iterators

    iterator begin() noexcept { return data_.GetAddress(); }

    iterator end() noexcept { return data_.GetAddress() + size_; }

    const_iterator begin() const noexcept { return data_.GetAddress(); }

    const_iterator end() const noexcept { return data_.GetAddress() + size_; }

    const_iterator cbegin() const noexcept { return begin(); }

    const_iterator cend() const noexcept { return end(); }

private:
    RawMemory<T> data_;
    size_t size_ = 0;

    void SwapData(RawMemory<T>& new_data) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        } else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    void ShiftSwap(RawMemory<T>& new_data, size_t drift) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), drift, new_data.GetAddress());
            std::uninitialized_move_n(data_ + drift, size_ - drift, new_data + drift + 1);
        } else {
            std::uninitialized_copy_n(data_.GetAddress(), drift, new_data.GetAddress());
            std::uninitialized_copy_n(data_ + drift, size_ - drift, new_data + drift + 1);
        }
        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }
};