#ifndef ONEFLOW_CORE_KERNEL_BBOX_UTIL_H_
#define ONEFLOW_CORE_KERNEL_BBOX_UTIL_H_

//#include <array>
#include "oneflow/core/common/util.h"
#include "oneflow/core/common/data_type.h"
#include "oneflow/core/operator/op_conf.pb.h"

namespace oneflow {

enum class BBoxCoord { kCorner = 0, kCenter, kGtCorner, kNormCorner, kNormCenter, kAlignCorner };

template<typename T, size_t N>
class ArrayBuffer;

template<typename T>
struct BBoxBase;

template<typename T>
struct ImIndexedBBoxBase;

template<typename T>
struct BBoxIf;

template<typename T, template<typename> class Base, BBoxCoord Coord>
struct BBoxImpl;

template<typename T>
struct BBoxDelta;

template<typename T, typename ImplT, size_t N>
class ArrayBufferBase {
 public:
  OF_DISALLOW_COPY_AND_MOVE(ArrayBufferBase);
  ArrayBufferBase() = delete;
  ~ArrayBufferBase() = delete;

  static const size_t ElemCnt = N;
  using ElemType = typename std::remove_const<T>::type;
  using ArrayType = std::array<ElemType, N>;
  using Impl = ImplT;

  static const Impl* Cast(const ElemType* ptr) { return reinterpret_cast<const Impl*>(ptr); }
  static Impl* MutCast(ElemType* ptr) { return reinterpret_cast<Impl*>(ptr); }

  const ArrayType& elem() const { return elem_; }
  ArrayType& mut_elem() { return elem_; }

 private:
  ArrayType elem_;
};

template<template<typename, template<typename> class, BBoxCoord> class BBoxImplT, typename T,
         template<typename> class BBoxBaseT, BBoxCoord Coord, size_t N>
class ArrayBuffer<BBoxImplT<T, BBoxBaseT, Coord>, N>
    : public ArrayBufferBase<T, BBoxImplT<T, BBoxBaseT, Coord>, N> {};

template<template<typename> class ImplT, typename T, size_t N>
class ArrayBuffer<ImplT<T>, N> : public ArrayBufferBase<T, ImplT<T>, N> {};

template<template<typename, template<typename> class, BBoxCoord> class BBoxImplT, typename T,
         BBoxCoord Coord>
struct BBoxBase<BBoxImplT<T, BBoxBase, Coord>>
    : public ArrayBuffer<BBoxImplT<T, BBoxBase, Coord>, 4> {
  T bbox_elem(size_t n) const { return this->elem()[n]; }
  void set_bbox_elem(size_t n, T val) { this->mut_elem()[n] = val; }
};

template<template<typename, template<typename> class, BBoxCoord> class BBoxImplT, typename T,
         BBoxCoord Coord>
struct ImIndexedBBoxBase<BBoxImplT<T, ImIndexedBBoxBase, Coord>>
    : public ArrayBuffer<BBoxImplT<T, ImIndexedBBoxBase, Coord>, 5> {
  T bbox_elem(size_t n) const { return this->elem()[n + 1]; }
  void set_bbox_elem(size_t n, T val) { this->mut_elem()[n + 1] = val; }

  T im_index() const { return this->elem()[0]; }
  void set_im_index(T index) { this->mut_elem()[0] = index; }
};

template<template<typename, template<typename> class, BBoxCoord> class BBoxImplT, typename T,
         template<typename> class BBoxBaseT, BBoxCoord Coord>
struct BBoxIf<BBoxImplT<T, BBoxBaseT, Coord>> {
  using Impl = BBoxImplT<T, BBoxBaseT, Coord>;

  Impl* impl() { return static_cast<Impl*>(this); }
  const Impl* impl() const { return static_cast<const Impl*>(this); }

  template<typename U>
  float InterOverUnion(const BBoxIf<U>* other) const {
    const float iw =
        std::min<float>(right(), other->right()) - std::max<float>(left(), other->left()) + 1.f;
    if (iw <= 0) { return 0.f; }
    const float ih =
        std::min<float>(bottom(), other->bottom()) - std::max<float>(top(), other->top()) + 1.f;
    if (ih <= 0) { return 0.f; }
    const float inter = iw * ih;
    return inter / (area() + other->area() - inter);
  }

  template<typename U, typename V>
  void Transform(const BBoxIf<U>* bbox, const BBoxDelta<V>* delta,
                 const BBoxRegressionWeights& bbox_reg_ws) {
    float dx = delta->dx() / bbox_reg_ws.weight_x();
    float dy = delta->dy() / bbox_reg_ws.weight_y();
    float dw = delta->dw() / bbox_reg_ws.weight_w();
    float dh = delta->dh() / bbox_reg_ws.weight_h();

    float pred_ctr_x = dx * bbox->width() + bbox->center_x();
    float pred_ctr_y = dy * bbox->height() + bbox->center_y();
    float pred_w = std::exp(dw) * bbox->width();
    float pred_h = std::exp(dh) * bbox->height();
    set_center_coord(pred_ctr_x, pred_ctr_y, pred_w, pred_h);
  }

  void Clip(const int64_t height, const int64_t width) {
    T left = std::max<T>(std::min<T>(this->left(), width - 1), 0);
    T top = std::max<T>(std::min<T>(this->top(), height - 1), 0);
    T right = std::max<T>(std::min<T>(this->right(), width - 1), 0);
    T bottom = std::max<T>(std::min<T>(this->bottom(), height - 1), 0);
    set_corner_coord(left, top, right, bottom);
  }

  T left() const { return impl()->left(); }
  T right() const { return impl()->right(); }
  T top() const { return impl()->top(); }
  T bottom() const { return impl()->bottom(); }
  T center_x() const { return impl()->center_x(); }
  T center_y() const { return impl()->center_y(); }
  T width() const { return impl()->width(); }
  T height() const { return impl()->height(); }
  T area() const { return width() * height(); }

  void set_center_coord(T ctr_x, T ctr_y, T w, T h) {
    impl()->set_center_coord(ctr_x, ctr_y, w, h);
  }
  void set_corner_coord(T left, T top, T right, T bottom) {
    impl()->set_corner_coord(left, top, right, bottom);
  }
};

template<typename T, template<typename> class BBoxBaseT>
struct BBoxImpl<T, BBoxBaseT, BBoxCoord::kCorner>
    : public BBoxIf<BBoxImpl<T, BBoxBaseT, BBoxCoord::kCorner>>,
      public BBoxBaseT<BBoxImpl<T, BBoxBaseT, BBoxCoord::kCorner>> {
  T left() const { return this->bbox_elem(0); }
  T top() const { return this->bbox_elem(1); }
  T right() const { return this->bbox_elem(2); }
  T bottom() const { return this->bbox_elem(3); }
  T center_x() const { return this->left() + 0.5f * this->width(); }
  T center_y() const { return this->top() + 0.5f * this->height(); }
  T width() const { return this->right() - this->left() + OneVal<T>::value; }
  T height() const { return this->bottom() - this->top() + OneVal<T>::value; }
  void set_center_coord(T ctr_x, T ctr_y, T w, T h) {
    this->set_bbox_elem(0, static_cast<T>(ctr_x - 0.5f * w));
    this->set_bbox_elem(1, static_cast<T>(ctr_y - 0.5f * h));
    this->set_bbox_elem(2, static_cast<T>(ctr_x + 0.5f * w - 1.f));
    this->set_bbox_elem(3, static_cast<T>(ctr_y + 0.5f * h - 1.f));
  }
  void set_corner_coord(T left, T top, T right, T bottom) {
    this->set_bbox_elem(0, left);
    this->set_bbox_elem(1, top);
    this->set_bbox_elem(2, right);
    this->set_bbox_elem(3, bottom);
  }
};

template<typename T, template<typename> class BBoxBaseT>
struct BBoxImpl<T, BBoxBaseT, BBoxCoord::kGtCorner>
    : public BBoxIf<BBoxImpl<T, BBoxBaseT, BBoxCoord::kGtCorner>>,
      public BBoxBaseT<BBoxImpl<T, BBoxBaseT, BBoxCoord::kGtCorner>> {
  T left() const { return this->bbox_elem(0); }
  T top() const { return this->bbox_elem(1); }
  T right() const { return this->bbox_elem(2) - 1; }
  T bottom() const { return this->bbox_elem(3) - 1; }
  T center_x() const { return this->left() + 0.5f * this->width(); }
  T center_y() const { return this->top() + 0.5f * this->height(); }
  T width() const { return this->right() - this->left() + OneVal<T>::value; }
  T height() const { return this->bottom() - this->top() + OneVal<T>::value; }
};

template<typename T>
struct BBoxDelta final : public ArrayBuffer<BBoxDelta<T>, 4> {
  T dx() const { return this->elem()[0]; }
  T dy() const { return this->elem()[1]; }
  T dw() const { return this->elem()[2]; }
  T dh() const { return this->elem()[3]; }

  void set_dx(T dx) { this->mut_elem()[0] = dx; }
  void set_dy(T dy) { this->mut_elem()[1] = dy; }
  void set_dw(T dw) { this->mut_elem()[2] = dw; }
  void set_dh(T dh) { this->mut_elem()[3] = dh; }

  template<typename U, typename V>
  void TransformInverse(const BBoxIf<U>* bbox, const BBoxIf<V>* target_bbox,
                        const BBoxRegressionWeights& bbox_reg_ws) {
    set_dx(bbox_reg_ws.weight_x() * (target_bbox->center_x() - bbox->center_x()) / bbox->width());
    set_dy(bbox_reg_ws.weight_y() * (target_bbox->center_y() - bbox->center_y()) / bbox->height());
    set_dw(bbox_reg_ws.weight_w() * std::log(target_bbox->width() / bbox->width()));
    set_dh(bbox_reg_ws.weight_h() * std::log(target_bbox->height() / bbox->height()));
  }
};

template<typename T>
struct BBoxWeights final : public ArrayBuffer<BBoxWeights<T>, 4> {
  inline T weight_x() const { return this->elem()[0]; }
  inline T weight_y() const { return this->elem()[1]; }
  inline T weight_w() const { return this->elem()[2]; }
  inline T weight_h() const { return this->elem()[3]; }

  inline void set_weight_x(T weight_x) { this->mut_elem()[0] = weight_x; }
  inline void set_weight_y(T weight_y) { this->mut_elem()[1] = weight_y; }
  inline void set_weight_w(T weight_w) { this->mut_elem()[2] = weight_w; }
  inline void set_weight_h(T weight_h) { this->mut_elem()[3] = weight_h; }
};

class IndexSequence {
 public:
  IndexSequence(size_t capacity, size_t size, int32_t* index_buf, bool init_index)
      : capacity_(capacity), size_(size), index_buf_(index_buf) {
    if (init_index) { std::iota(index_buf_, index_buf_ + size_, 0); }
  }
  IndexSequence(size_t capacity, int32_t* index_buf, bool init_index = false)
      : IndexSequence(capacity, capacity, index_buf, init_index) {}

  void Assign(const IndexSequence& other) {
    CHECK_LE(other.size(), capacity_);
    FOR_RANGE(size_t, i, 0, other.size()) { index_buf_[i] = other.GetIndex(i); }
    size_ = other.size();
  }

  IndexSequence Slice(size_t begin, size_t end) const {
    CHECK_LE(begin, end);
    CHECK_LE(end, size_);
    return IndexSequence(end - begin, index_buf_ + begin, false);
  }

  void Concat(const IndexSequence& other) {
    CHECK_LE(other.size(), capacity_ - size_);
    FOR_RANGE(size_t, i, 0, other.size()) { index_buf_[size_ + i] = other.GetIndex(i); }
    size_ += other.size();
  }

  size_t Find(const std::function<bool(int32_t)>& Condition) const {
    FOR_RANGE(size_t, i, 0, this->size()) {
      if (Condition(GetIndex(i))) { return i; }
    }
    return this->size();
  }

  void Filter(const std::function<bool(int32_t)>& FilterFunc) {
    size_t keep_num = 0;
    FOR_RANGE(size_t, i, 0, size_) {
      int32_t cur_index = GetIndex(i);
      if (!FilterFunc(cur_index)) {
        // keep_num <= i so index_ptr_ never be written before read
        index()[keep_num++] = cur_index;
      }
    }
    size_ = keep_num;
  }

  void Sort(const std::function<bool(int32_t, int32_t)>& Compare) {
    std::sort(index_buf_, index_buf_ + size_,
              [&](int32_t lhs_index, int32_t rhs_index) { return Compare(lhs_index, rhs_index); });
  }

  void Shuffle(size_t begin, size_t end) {
    CHECK_LE(begin, end);
    CHECK_LE(end, size_);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(index_buf_ + begin, index_buf_ + end, gen);
  }

  void Shuffle() { Shuffle(0, size_); }

  void ForEach(const std::function<bool(int32_t)>& DoNext) {
    FOR_RANGE(size_t, i, 0, size_) {
      if (!DoNext(GetIndex(i))) { break; }
    }
  }

  void Truncate(size_t size) {
    CHECK_LE(size, capacity_);
    size_ = size;
  }

  void PushBack(int32_t index) {
    CHECK_LT(size_, capacity_);
    index_buf_[size_++] = index;
  }

  int32_t GetIndex(size_t n) const {
    CHECK_LT(n, size_);
    return index_buf_[n];
  }

  const int32_t* index() const { return index_buf_; }
  int32_t* index() { return index_buf_; }
  size_t capacity() const { return capacity_; }
  size_t size() const { return size_; };

 private:
  const size_t capacity_;
  size_t size_;
  int32_t* index_buf_;
};

template<typename Indices, typename BBox>
class BBoxIndices;

template<typename Indices, typename T, template<typename> class Base, BBoxCoord Coord,
         template<typename, template<typename> class, BBoxCoord> class Impl>
class BBoxIndices<Indices, Impl<T, Base, Coord>> : public Indices {
 public:
  using BBox = Impl<T, Base, Coord>;
  template<typename U>
  using BBoxT = Impl<U, Base, Coord>;

  BBoxIndices(const Indices& inds, T* bbox_buf) : Indices(inds), bbox_buf_(bbox_buf) {}

  const BBox* GetBBox(size_t n) const {
    CHECK_LT(n, this->size());
    return BBox::Cast(bbox_buf_) + this->GetIndex(n);
  }
  BBox* GetMutBBox(size_t n) {
    CHECK_LT(n, this->size());
    return BBox::MutCast(bbox_buf_) + this->GetIndex(n);
  }
  const BBox* bbox(int32_t index) const {
    CHECK_GE(index, 0);
    return BBox::Cast(bbox_buf_) + index;
  }
  BBox* mut_bbox(int32_t index) {
    CHECK_GE(index, 0);
    return BBox::MutCast(bbox_buf_) + index;
  }
  const T* bbox() const { return bbox_buf_; }
  T* mut_bbox() { return bbox_buf_; }

 private:
  T* bbox_buf_;
};

template<typename Indices>
class LabelIndices : public Indices {
 public:
  LabelIndices(const Indices& inds, int32_t* label_buf) : Indices(inds), label_buf_(label_buf) {}

  void FillLabels(int32_t begin_index, int32_t end_index, int32_t label) {
    CHECK_GE(begin_index, 0);
    CHECK_LE(begin_index, end_index);
    CHECK_LE(end_index, this->capacity());
    std::fill(label_buf_ + begin_index, label_buf_ + end_index, label);
  }
  void FillLabels(int32_t label) { FillLabels(0, this->capacity(), label); }

  int32_t GetLabel(size_t n) const {
    CHECK_LT(n, this->size());
    return label(this->GetIndex(n));
  }
  void SetLabel(size_t n, int32_t label) {
    CHECK_LT(n, this->size());
    set_label(this->GetIndex(n), label);
  }
  int32_t label(int32_t index) const {
    CHECK_GE(index, 0);
    return label_buf_[index];
  }
  void set_label(int32_t index, int32_t label) {
    CHECK_GE(index, 0);
    label_buf_[index] = label;
  }
  const int32_t* label() const { return label_buf_; }
  int32_t* mut_label() { return label_buf_; }

 private:
  int32_t* label_buf_;
};

template<typename Indices, typename T>
class ScoreIndices : public Indices {
 public:
  ScoreIndices(const Indices& inds, T* score_buf) : Indices(inds), score_buf_(score_buf) {}

  void SortByScore(const std::function<bool(T, T)>& Compare) {
    this->Sort([&](int32_t lhs_index, int32_t rhs_index) {
      return Compare(score(lhs_index), score(rhs_index));
    });
  }

  size_t FindByScore(const std::function<bool(T)>& Condition) {
    return this->Find([&](int32_t index) { return Condition(score(index)); });
  }

  void FilterByScore(const std::function<bool(size_t, int32_t, T)>& FilterFunc) {
    this->Filter([&](size_t n, int32_t index) { return FilterFunc(n, index, score(index)); });
  }

  T GetScore(size_t n) const {
    CHECK_LT(n, this->size());
    return score(this->GetIndex(n));
  }
  void SetScore(size_t n, T score) {
    CHECK_LT(n, this->size());
    set_score(this->GetIndex(n));
  }
  T score(int32_t index) const {
    CHECK_GE(index, 0);
    return score()[index];
  }
  void set_score(int32_t index, T score) {
    CHECK_GE(index, 0);
    mut_score()[index] = score;
  }
  const T* score() const { return score_buf_; }
  T* mut_score() { return score_buf_; }

 private:
  T* score_buf_;
};

template<typename Indices>
class MaxOverlapIndices : public Indices {
 public:
  MaxOverlapIndices(const Indices& inds, float* max_overlap_buf,
                    int32_t* max_overlap_with_index_buf, bool init)
      : Indices(inds),
        max_overlap_buf_(max_overlap_buf),
        max_overlap_with_index_buf_(max_overlap_with_index_buf) {
    if (init) {
      memset(max_overlap_buf, 0, this->capacity() * sizeof(float));
      std::fill(max_overlap_with_index_buf_, max_overlap_with_index_buf_ + this->capacity(), -1);
    }
  }

  void TryUpdateMaxOverlap(int32_t index, int32_t with_index, float overlap,
                           const std::function<void()>& DoUpdateHandle = []() {}) {
    CHECK_GE(index, 0);
    if (overlap > max_overlap(index)) {
      set_max_overlap(index, overlap);
      set_max_overlap_with_index(index, with_index);
      DoUpdateHandle();
    }
  }

  float GetMaxOverlap(size_t n) const {
    CHECK_LT(n, this->size());
    return max_overlap(this->GetIndex(n));
  }
  int32_t GetMaxOverlapWithIndex(size_t n) const {
    CHECK_LT(n, this->size());
    return max_overlap_with_index(this->GetIndex(n));
  }
  float max_overlap(int32_t index) const {
    if (index < 0) { return 1; }
    return max_overlap_buf_[index];
  }
  void set_max_overlap(int32_t index, float overlap) {
    CHECK_GE(index, 0);
    max_overlap_buf_[index] = overlap;
  }
  int32_t max_overlap_with_index(int32_t index) const {
    if (index < 0) { return -index - 1; }
    return max_overlap_with_index_buf_[index];
  }
  void set_max_overlap_with_index(int32_t index, int32_t with_index) {
    CHECK_GE(index, 0);
    max_overlap_with_index_buf_[index] = with_index;
  }

 private:
  float* max_overlap_buf_;
  int32_t* max_overlap_with_index_buf_;
};

template<typename BBox, typename GtBBox>
void ForEachOverlapBetweenBoxesAndGtBoxes(
    const BBoxIndices<IndexSequence, BBox>& boxes,
    const BBoxIndices<IndexSequence, GtBBox>& gt_boxes,
    const std::function<void(int32_t, int32_t, float)>& Handler) {
  FOR_RANGE(size_t, i, 0, gt_boxes.size()) {
    FOR_RANGE(size_t, j, 0, boxes.size()) {
      float overlap = boxes.GetBBox(j)->InterOverUnion(gt_boxes.GetBBox(i));
      Handler(boxes.GetIndex(j), gt_boxes.GetIndex(i), overlap);
    }
  }
}

template<typename BBox>
struct BBoxUtil final {
  using T = typename BBox::ElemType;
  using BBoxIndicesT = BBoxIndices<IndexSequence, BBox>;

  static size_t GenerateAnchors(const AnchorGeneratorConf& conf, T* anchors_ptr);
  static void Nms(float thresh, const BBoxIndicesT& pre_nms_bbox_inds,
                  BBoxIndicesT& post_nms_bbox_inds);
};

}  // namespace oneflow

#endif  // ONEFLOW_CORE_KERNEL_BBOX_UTIL_H_
