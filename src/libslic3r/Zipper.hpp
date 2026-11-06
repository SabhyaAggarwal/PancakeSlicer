#ifndef ZIPPER_HPP
#define ZIPPER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>
#include <type_traits>

namespace Slic3r {


class Zipper {
public:

    enum e_compression {
        NO_COMPRESSION,
        FAST_COMPRESSION,
        TIGHT_COMPRESSION
    };

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    std::string m_data;
    std::string m_entry;
    e_compression m_compression;

public:


    explicit Zipper(const std::string& zipfname,
                    e_compression level = FAST_COMPRESSION);
    ~Zipper();


    Zipper(const Zipper&) = delete;
    Zipper& operator=(const Zipper&) = delete;





    Zipper(Zipper &&m);
    Zipper& operator=(Zipper &&m);




    void add_entry(const std::string& name);



    void add_entry(const std::string& name, const void* data, size_t bytes);





    template<class T> inline
    typename std::enable_if<std::is_arithmetic<T>::value, Zipper&>::type
    operator<<(T &&val) {
        return this->operator<<(std::to_string(std::forward<T>(val)));
    }



    template<class T> inline
    typename std::enable_if<!std::is_arithmetic<T>::value, Zipper&>::type
    operator<<(T &&val) {
        if(m_data.empty()) m_data = std::forward<T>(val);
        else m_data.append(val);
        return *this;
    }










    void finish_entry();

    void finalize();

    const std::string & get_filename() const;
};


}

#endif
