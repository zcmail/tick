#ifndef LIB_INCLUDE_TICK_ARRAY_SARRAY_H_
#define LIB_INCLUDE_TICK_ARRAY_SARRAY_H_

// License: BSD 3 clause

/** @file */

#include "array.h"

///////////////////////////////////////////////////////////////////////////////////
//
//  SArray
//
///////////////////////////////////////////////////////////////////////////////////

/*! \class SArray
 * \brief Template class for 1d non sparse arrays of type `T` fully shareable
 * with Python.
 * This is the array class that introduces the ability to share its allocation
 * with Python. \warning You should always use the associated smart pointer
 * class `SArrayPtr` and never access this class directly. Thus the only
 * constructor to be used is the `SArray<T>::new_ptr()` constructor. In that way
 * you can also share the allocations within C++ (using a reference counter).
 */
template <typename T>
class SArray : public Array<T> {
 protected:
  using K = typename Array<T>::K;
  using Array<T>::_size;
  using Array<T>::is_data_allocation_owned;
  using Array<T>::_data;
  using Array<T>::get_data_index;
  using Array<T>::set_data_index;

#ifdef PYTHON_LINK
  //! @brief The (eventual) Python owner of the array _data;
  //! If ==nullptr then it is self-owned
  void *_data_owner = nullptr;
#endif

 public:
  using COMPARATOR = Array_Comparator<SArray<T>>;

 public:
#ifdef PYTHON_LINK
  void give_data_ownership(void *owner);

  //! @brief returns the python object which is owner _data allocation
  inline void *data_owner() { return _data_owner; }

  //! @brief Sets the data allocation owner with either a numpy array or nullptr
  //! \param data A pointer to the data array
  //! \param size The size
  //! \param owner A pointer to the numpy array (if nullptr, then the data
  //! allocation is owned by the object itself).
  virtual void set_data(T *data, ulong size, void *owner = nullptr);
#else
  //! @brief Sets the data. After this call, allocation is owned by the SArray.
  //! \param data A pointer to the data array
  //! \param size The size
  virtual void set_data(T *data, ulong size);
#endif

 protected:
#ifdef DEBUG_SHAREDARRAY
  static ulong n_allocs;
#endif

 public:
  // Constructors :
  //     One should only use the constructors
  //     SArray<T>::new_ptr(size) which returns a shared pointer
  //     SArray<T>::new_ptr(array) which returns a shared pointer
  //

  //! \cond
  // Constructor of an array of size `size` : not to be used directly
  // If size != 0 allocation is performed
  explicit SArray(ulong size = 0);
  //! \endcond

  //! @brief The only constructor to be used
  //! \param size : the size of the array to be built
  //! \return A shared pointer to the corresponding shared array of the given
  //! size (thus allocation is performed)
  static std::shared_ptr<SArray<T>> new_ptr(ulong size = 0) {
    return std::make_shared<SArray<T>>(size);
  }

  //! @brief The only constructor to be used from an Array<T>
  //! \param a : The array the data are copied from
  //! \return A shared pointer to the corresponding shared array
  //! \warning The data are copied
  static std::shared_ptr<SArray<T>> new_ptr(Array<T> &a);

 protected:
  /**
   * @brief clears the array without deallocating the data pointer.
   * Returns this pointer if it should be deallocated otherwise returns
   * nullptr
   */
  virtual T *_clear();

 public:
  //! @brief clears the corresponding allocation (size becomes 0)
  virtual void clear();

  //! @brief Destructor
  virtual ~SArray();

  // The following constructors should not be used as we only use SArray with
  // the associated shared_ptr
  SArray(const SArray<T> &other) = delete;
  SArray(const SArray<T> &&other) = delete;

  // Some friendship for set_data/give_data_ownership access !
  friend std::shared_ptr<SArray<T>> Array<T>::as_sarray_ptr();
};

#ifdef PYTHON_LINK
template <typename T>
void SArray<T>::set_data(T *data, ulong size, void *owner) {
  clear();
  _data = data;
  _size = size;
  give_data_ownership(owner);
}
#else
template <typename T>
void SArray<T>::set_data(T *data, ulong size) {
  clear();
  _data = data;
  _size = size;
  is_data_allocation_owned = true;
}
#endif

#ifdef PYTHON_LINK
template <typename T>
void SArray<T>::give_data_ownership(void *data_owner) {
#ifdef DEBUG_SHAREDARRAY
  if (_data_owner == nullptr)
    std::cout << "SArray : SetOwner owner=" << data_owner << " on " << this
              << std::endl;
  else
    std::cout << "SArray : ChangeOwner owner=" << _data_owner << " -> "
              << data_owner << " on " << this << std::endl;
#endif
  _data_owner = data_owner;
  if (_data_owner) {
    PYINCREF(_data_owner);
    is_data_allocation_owned = false;
  } else {
    is_data_allocation_owned = true;
  }
}
#endif

#ifdef DEBUG_SHAREDARRAY
template <typename T>
ulong SArray<T>::n_allocs = 0;
#endif

// TODO : bof !
template <typename T>
std::ostream &operator<<(std::ostream &Str, SArray<T> *v) {
#ifdef DEBUG_SHAREDARRAY
  Str << "SArray<" << typeid(T).name() << ">(" << reinterpret_cast<void *>(v)
      << ",size=" << v->size() << ")";
#else
  Str << "SArray<" << typeid(T).name() << ">(" << reinterpret_cast<void *>(v)
      << ",size=" << v->size() << ")";
#endif
  return Str;
}

// In order to create a Shared array
template <typename T>
SArray<T>::SArray(ulong size) : Array<T>(size) {
#ifdef PYTHON_LINK
  _data_owner = nullptr;
#endif
#ifdef DEBUG_SHAREDARRAY
  n_allocs++;
  std::cout << "SArray Constructor (->#" << n_allocs
            << ") : SArray(size=" << _size << ") --> " << this << std::endl;
#endif
}

// Constructor from an Array (copy is made)
template <typename T>
std::shared_ptr<SArray<T>> SArray<T>::new_ptr(Array<T> &a) {
  if (a.size() == 0) return SArray<T>::new_ptr();
  std::shared_ptr<SArray<T>> aptr = SArray<T>::new_ptr(a.size());
  memcpy(aptr->data(), a.data(), sizeof(T) * a.size());
  return aptr;
}

// Clears the array without desallocating the data pointer.
// Returns this pointer if it should be desallocated otherwise returns NULL
template <typename T>
T *SArray<T>::_clear() {
  bool result = false;

  // Case not empty
  if (_data) {
#ifdef PYTHON_LINK
    // We have to deal with Python owner if any
    if (_data_owner != nullptr) {
      PYDECREF(_data_owner);
#ifdef DEBUG_SHAREDARRAY
      std::cout << "SArray Clear : " << this
                << " decided not to free data=" << _data
                << " since owned by owner=" << _data_owner << std::endl;
#endif
      _data_owner = nullptr;
    } else {
#ifdef DEBUG_SHAREDARRAY
      std::cout << "SArray Clear : " << this
                << " decided to free data=" << _data << std::endl;
#endif
      result = true;
    }
#else
#ifdef DEBUG_SHAREDARRAY
    std::cout << "SArray Clear : " << this << " decided to free data=" << _data
              << std::endl;
#endif
    result = true;
#endif
  }
  _size = 0;

  is_data_allocation_owned = true;

  return result ? _data : nullptr;
}

// clear the array (including data if necessary)
template <typename T>
void SArray<T>::clear() {
  if (_clear()) {
    TICK_PYTHON_FREE(_data);
  }
  _data = nullptr;
}

// Destructor
template <typename T>
SArray<T>::~SArray<T>() {
#ifdef DEBUG_SHAREDARRAY
  n_allocs--;
  std::cout << "SArray<" << typeid(T).name() << "> Destructor (->#" << n_allocs
            << ") : ~SArray on " << this << std::endl;
#endif
  clear();
}

// @brief Returns a shared pointer to a SArray encapsulating the array
// \warning : The ownership of the data is given to the returned structure
// THUS the array becomes a view.
// \warning : This method cannot be called on a view
template <typename T>
std::shared_ptr<SArray<T>> Array<T>::as_sarray_ptr() {
  if (!is_data_allocation_owned)
    TICK_ERROR(
        "This method cannot be called on an object that does not own its "
        "allocations");

  std::shared_ptr<SArray<T>> arrayptr = SArray<T>::new_ptr();
  arrayptr->set_data(_data, _size);
  is_data_allocation_owned = false;
  return arrayptr;
}

// Instanciations

/**
 * \defgroup SArray_typedefs_mod SArray related typedef
 * \brief List of all the instantiations of the SArray template and associated
 *  shared pointers and 1d and 2d List of these classes
 * @{
 */

/**
 * @}
 */
/** @defgroup sarray_sub_mod The instantiations of the SArray template
 *  @ingroup SArray_typedefs_mod
 * @{
 */
/**
 * @}
 */
/** @defgroup sarrayptr_sub_mod The shared pointer array classes
 *  @ingroup SArray_typedefs_mod
 * @{
 */
/**
 * @}
 */
/** @defgroup sarrayptrlist1d_sub_mod The classes for dealing with 1d-list of
 * shared pointer arrays
 *  @ingroup SArray_typedefs_mod
 * @{
 */
// @brief The basic SArrayList1D classes
/**
 * @}
 */
/** @defgroup sarrayptrlist2d_sub_mod The classes for dealing with 2d-list of
 * shared pointer arrays
 *  @ingroup SArray_typedefs_mod
 * @{
 */
// @brief The basic SArrayList2D classes
/**
 * @}
 */
#define VEC_PTR_CMP(TYPE) template class Array_Comparator<SArray<TYPE>>
#if defined(__APPLE__) || defined(_WIN32)
VEC_PTR_CMP(unsigned long);
#endif

#define SARRAY_DEFINE_TYPE(TYPE, NAME)                                  \
  typedef SArray<TYPE> SArray##NAME;                                    \
  typedef std::shared_ptr<SArray##NAME> SArray##NAME##Ptr;              \
  typedef std::vector<SArray##NAME##Ptr> SArray##NAME##PtrList1D;       \
  typedef std::vector<SArray##NAME##PtrList1D> SArray##NAME##PtrList2D; \
  VEC_PTR_CMP(TYPE);

SARRAY_DEFINE_TYPE(double, Double);
SARRAY_DEFINE_TYPE(float, Float);
SARRAY_DEFINE_TYPE(int32_t, Int);
SARRAY_DEFINE_TYPE(uint32_t, UInt);
SARRAY_DEFINE_TYPE(int16_t, Short);
SARRAY_DEFINE_TYPE(uint16_t, UShort);
SARRAY_DEFINE_TYPE(int64_t, Long);
SARRAY_DEFINE_TYPE(ulong, ULong);
SARRAY_DEFINE_TYPE(std::atomic<double>, AtomicDouble);
SARRAY_DEFINE_TYPE(std::atomic<float>, AtomicFloat);

#undef SARRAY_DEFINE_TYPE
#undef VEC_PTR_CMP

#define INSTANTIATE_SARRAY(SARRAY_TYPE, C_TYPE) \
  template std::ostream &operator<<<C_TYPE>(std::ostream &Str, SArray<C_TYPE> *v)

INSTANTIATE_SARRAY(SArrayDoublePtr, double);
INSTANTIATE_SARRAY(SArrayFloatPtr, float);
INSTANTIATE_SARRAY(SArrayIntPtr, std::int32_t);
INSTANTIATE_SARRAY(SArrayUIntPtr, std::uint32_t);
INSTANTIATE_SARRAY(SArrayShortPtr, std::int16_t);
INSTANTIATE_SARRAY(SArrayUShortPtr, std::uint16_t);
INSTANTIATE_SARRAY(SArrayLongPtr, std::int64_t);
INSTANTIATE_SARRAY(SArrayULongPtr, ulong);

#undef INSTANTIATE_SARRAY

#endif  // LIB_INCLUDE_TICK_ARRAY_SARRAY_H_
