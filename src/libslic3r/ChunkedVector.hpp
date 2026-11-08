#pragma once

#include <cstddef>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace Slic3r {

template<class T, size_t ChunkBits = 15> class ChunkedVector
{
public:
    static constexpr size_t chunk_size = size_t(1) << ChunkBits;
    using value_type      = T;
    using size_type       = size_t;
    using difference_type = std::ptrdiff_t;
    using reference       = T &;
    using const_reference = const T &;

    template<class Owner, class Ref> class Iterator
    {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = T;
        using difference_type   = std::ptrdiff_t;
        using reference         = Ref;
        using pointer           = std::remove_reference_t<Ref> *;

        Iterator() = default;
        Iterator(Owner *owner, size_t i) : m_owner(owner), m_i(i) {}
        template<class O, class R, class = std::enable_if_t<std::is_convertible_v<O *, Owner *>>>
        Iterator(const Iterator<O, R> &other) : m_owner(other.owner()), m_i(other.index()) {}

        reference operator*() const { return (*m_owner)[m_i]; }
        pointer   operator->() const { return &(*m_owner)[m_i]; }
        reference operator[](difference_type n) const { return (*m_owner)[m_i + n]; }
        Iterator &operator++() { ++m_i; return *this; }
        Iterator &operator--() { --m_i; return *this; }
        Iterator  operator++(int) { Iterator it = *this; ++m_i; return it; }
        Iterator  operator--(int) { Iterator it = *this; --m_i; return it; }
        Iterator &operator+=(difference_type n) { m_i += n; return *this; }
        Iterator &operator-=(difference_type n) { m_i -= n; return *this; }
        friend Iterator operator+(Iterator it, difference_type n) { return it += n; }
        friend Iterator operator+(difference_type n, Iterator it) { return it += n; }
        friend Iterator operator-(Iterator it, difference_type n) { return it -= n; }
        friend difference_type operator-(const Iterator &a, const Iterator &b) { return difference_type(a.m_i) - difference_type(b.m_i); }
        friend bool operator==(const Iterator &a, const Iterator &b) { return a.m_i == b.m_i; }
        friend bool operator!=(const Iterator &a, const Iterator &b) { return a.m_i != b.m_i; }
        friend bool operator<(const Iterator &a, const Iterator &b) { return a.m_i < b.m_i; }
        friend bool operator>(const Iterator &a, const Iterator &b) { return a.m_i > b.m_i; }
        friend bool operator<=(const Iterator &a, const Iterator &b) { return a.m_i <= b.m_i; }
        friend bool operator>=(const Iterator &a, const Iterator &b) { return a.m_i >= b.m_i; }

        Owner *owner() const { return m_owner; }
        size_t index() const { return m_i; }

    private:
        Owner *m_owner{ nullptr };
        size_t m_i{ 0 };
    };
    using iterator       = Iterator<ChunkedVector, T &>;
    using const_iterator = Iterator<const ChunkedVector, const T &>;

    ChunkedVector() = default;
    ChunkedVector(ChunkedVector &&other) noexcept : m_chunks(std::move(other.m_chunks)), m_size(std::exchange(other.m_size, 0)) {}
    ChunkedVector &operator=(ChunkedVector &&other) noexcept
    {
        m_chunks = std::move(other.m_chunks);
        m_size   = std::exchange(other.m_size, 0);
        other.m_chunks.clear();
        return *this;
    }
    ChunkedVector(const ChunkedVector &other) { *this = other; }
    ChunkedVector &operator=(const ChunkedVector &other)
    {
        if (this != &other) {
            resize(other.size());
            for (size_t i = 0; i < m_size; ++i)
                (*this)[i] = other[i];
            shrink_to_fit();
        }
        return *this;
    }

    T       &operator[](size_t i) { return m_chunks[i >> ChunkBits][i & (chunk_size - 1)]; }
    const T &operator[](size_t i) const { return m_chunks[i >> ChunkBits][i & (chunk_size - 1)]; }
    T       &front() { return (*this)[0]; }
    const T &front() const { return (*this)[0]; }
    T       &back() { return (*this)[m_size - 1]; }
    const T &back() const { return (*this)[m_size - 1]; }

    iterator       begin() { return { this, 0 }; }
    iterator       end() { return { this, m_size }; }
    const_iterator begin() const { return { this, 0 }; }
    const_iterator end() const { return { this, m_size }; }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    size_t size() const { return m_size; }
    bool   empty() const { return m_size == 0; }
    size_t capacity() const { return m_chunks.size() * chunk_size; }

    void push_back(const T &v) { grow(m_size + 1); (*this)[m_size++] = v; }
    void push_back(T &&v) { grow(m_size + 1); (*this)[m_size++] = std::move(v); }
    template<class... Args> T &emplace_back(Args &&...args)
    {
        grow(m_size + 1);
        T &slot = (*this)[m_size++];
        slot    = T{ std::forward<Args>(args)... };
        return slot;
    }
    void pop_back() { (*this)[--m_size] = T(); }
    void resize(size_t n)
    {
        grow(n);
        for (size_t i = n; i < m_size; ++i)
            (*this)[i] = T();
        m_size = n;
    }
    void reserve(size_t n) { m_chunks.reserve((n + chunk_size - 1) >> ChunkBits); }
    iterator erase(const_iterator pos)
    {
        for (size_t i = pos.index(); i + 1 < m_size; ++i)
            (*this)[i] = std::move((*this)[i + 1]);
        pop_back();
        return { this, pos.index() };
    }
    void clear()
    {
        m_chunks.clear();
        m_chunks.shrink_to_fit();
        m_size = 0;
    }
    void shrink_to_fit()
    {
        m_chunks.resize((m_size + chunk_size - 1) >> ChunkBits);
        m_chunks.shrink_to_fit();
    }

private:
    void grow(size_t n)
    {
        while (capacity() < n)
            m_chunks.emplace_back(new T[chunk_size]());
    }

    std::vector<std::unique_ptr<T[]>> m_chunks;
    size_t                            m_size{ 0 };
};

}
