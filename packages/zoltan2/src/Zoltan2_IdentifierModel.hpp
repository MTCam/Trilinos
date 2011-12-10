// @HEADER
// ***********************************************************************
//
//                Copyright message goes here.   TODO
//
// ***********************************************************************
// @HEADER

/*! \file Zoltan2_IdentifierModel.hpp

    \brief The interface and implementations of a simple identifier model.
*/


#ifndef _ZOLTAN2_IDENTIFIERMODEL_HPP_
#define _ZOLTAN2_IDENTIFIERMODEL_HPP_

#include <Zoltan2_Model.hpp>
#include <Zoltan2_MatrixInput.hpp>
#include <Zoltan2_IdentifierInput.hpp>

namespace Zoltan2 {

/*! Zoltan2::IdentifierModel
    \brief This class provides simple IDs and weights to the Zoltan2 algorithm.

    The template parameter is an Input Adapter.  Input adapters are
    templated on the basic user input type.
*/
template <typename Adapter>
class IdentifierModel : public Model<Adapter> 
{
private:

public:

  typedef typename Adapter::scalar_t  scalar_t;
  typedef typename Adapter::gno_t     gno_t;
  
  IdentifierModel(){
    throw std::logic_error("a specific instantiation should be used");
  }

  /*! Returns the number identifierson this process.
   */
  size_t getLocalNumIdentifiers() const { return 0; }

  /*! Returns the global number identifiers.
   */
  global_size_t getGlobalNumIdentifiers() const { return 0; }

  /*! Returns the dimension (0 or greater) of identifier weights.
   */
  int getIdentifierWeightDim() const { return 0; }

  /*! Sets pointers to this process' identifier Ids and their weights.
      \param Ids will on return point to the list of the global Ids for
        each identifier on this process.
      \param wgts will on return point to a list of the weight or weights
         associated with each identifier in the Ids list.  Weights are listed by
         identifier by weight component.
       \return The number of ids in the Ids list.
   */

  size_t getIdentifierList( ArrayView<const gno_t> &Ids,
    ArrayView<const scalar_t> &wgts) const { return 0; }

  ////////////////////////////////////////////////////
  // The Model interface.
  ////////////////////////////////////////////////////

  size_t getLocalNumObjects() const
  {
    return getLocalNumIdentifiers();
  }

  size_t getGlobalNumObjects() const
  {
    return getGlobalNumIdentifiers();
  }

  void getGlobalObjectIds(ArrayView<const gno_t> &gnos) const { return ; }


};

template <typename User>
class IdentifierModel<IdentifierInput<User> > : public Model<IdentifierInput<User> >
{
public:

  typedef typename IdentifierInput<User>::scalar_t  scalar_t;
  typedef typename IdentifierInput<User>::gno_t     gno_t;
  typedef typename IdentifierInput<User>::lno_t     lno_t;
  typedef typename IdentifierInput<User>::gid_t     gid_t;
  typedef IdentifierMap<gid_t, lno_t, gno_t> idmap_t;
  
  IdentifierModel( const IdentifierInput<User> *ia, 
    const RCP<const Environment> &env, bool gnosMustBeConsecutive=false):
      gnosAreGids_(false), weightDim_(0), numGlobalIdentifiers_(), env_(env), 
      comm_(env->comm_), gids_(), weights_(), gnos_(), gnosConst_()
  {
    size_t nLocalIds;
    const gid_t *gids;
    const scalar_t *wgts;

    weightDim_ = ia->getIdentifierWeightDim();

    try{
      nLocalIds = ia->getIdentifierView(gids, wgts);
    }
    Z2_FORWARD_EXCEPTIONS;

    if (nLocalIds){
      gids_ = arcp(const_cast<gid_t *>(gids), 0, nLocalIds, false);
  
      if (wgts){
        weights_ = arcp(const_cast<scalar_t *>(wgts), 0, nLocalIds*weightDim_, 
          false);
      }
    }

    RCP<const idmap_t> idMap;

    try{
      idMap = rcp(new idmap_t(env_, gids_, gnosMustBeConsecutive));
    }
    Z2_FORWARD_EXCEPTIONS;

    gnosAreGids_ = idMap->gnosAreGids();

    this->setIdentifierMap(idMap);

    gno_t lsum = nLocalIds;
    reduceAll<int, gno_t>(*comm_, Teuchos::REDUCE_SUM, 1, &lsum,
      &numGlobalIdentifiers_);

    if (!gnosAreGids_ && nLocalIds>0){
      gno_t *tmpGno = new gno_t [nLocalIds];
      Z2_LOCAL_MEMORY_ASSERTION(*env_, nLocalIds, tmpGno);
      gnos_ = arcp(tmpGno, 0, gids_.size(), true);

      try{
        ArrayRCP<gid_t> gidsNonConst = arcp_const_cast<gid_t>(gids_);
        idMap->gidTranslate( gids_(0,nLocalIds),  gnos_(0,nLocalIds), 
          TRANSLATE_APP_TO_LIB);
      }
      Z2_FORWARD_EXCEPTIONS;
    }

    gnosConst_ = arcp_const_cast<const gno_t>(gnos_);
  }

  /*! Returns the number identifierson this process.
   */
  size_t getLocalNumIdentifiers() const { return gids_.size(); }

  /*! Returns the global number identifiers.
   */
  global_size_t getGlobalNumIdentifiers() const {return numGlobalIdentifiers_;}

  /*! Returns the dimension (0 or greater) of identifier weights.
   */
  int getIdentifierWeightDim() const { return weightDim_; }

  /*! Sets pointers to this process' identifier Ids and their weights.
      \param Ids will on return point to the list of the global Ids for
        each identifier on this process.
      \param wgts will on return point to a list of the weight or weights
         associated with each identifier in the Ids list.  Weights are listed by
         identifier by weight component.
       \return The number of ids in the Ids list.
   */

  size_t getIdentifierList( ArrayView<const gno_t> &Ids,
    ArrayView<const scalar_t> &wgts) const 
  {
    size_t n = getLocalNumIdentifiers();
    size_t nweights = n * weightDim_;

    Ids =  ArrayView<const gno_t>(Teuchos::null);
    wgts = ArrayView<const scalar_t>(Teuchos::null);

    if (n){
      if (gnosAreGids_)
        Ids = gids_(0, n);
      else
        Ids = gnosConst_(0, n);

      if (nweights)
        wgts = weights_;
    }
    
    return n;
  }

  ////////////////////////////////////////////////////
  // The Model interface.
  ////////////////////////////////////////////////////

  size_t getLocalNumObjects() const
  {
    return getLocalNumIdentifiers();
  }

  size_t getGlobalNumObjects() const
  {
    return getGlobalNumIdentifiers();
  }

  void getGlobalObjectIds(ArrayView<const gno_t> &gnos) const 
  { 
    ArrayView<const scalar_t> weights;
    getIdentifierList(gnos, weights);
  }

private:

  bool gnosAreGids_;
  int weightDim_;
  gno_t numGlobalIdentifiers_;
  const RCP<const Environment> env_;
  const RCP<const Comm<int> > comm_;
  ArrayRCP<const gid_t> gids_;
  ArrayRCP<const scalar_t> weights_;
  ArrayRCP<gno_t> gnos_;
  ArrayRCP<const gno_t> gnosConst_;
};

template <typename User>
class IdentifierModel<MatrixInput<User> > : public Model<MatrixInput<User> >
{
public:

  typedef typename MatrixInput<User>::scalar_t  scalar_t;
  typedef typename MatrixInput<User>::gno_t     gno_t;
  typedef typename MatrixInput<User>::lno_t     lno_t;
  typedef typename MatrixInput<User>::gid_t     gid_t;
  typedef IdentifierMap<gid_t, lno_t, gno_t> idmap_t;
  
  IdentifierModel( const MatrixInput<User> *ia, 
    const RCP<const Environment> &env, bool gnosMustBeConsecutive=false):
      gnosAreGids_(false), weightDim_(0), numGlobalIdentifiers_(), env_(env), 
      comm_(env->comm_), gids_(), weights_(), gnos_(), gnosConst_()
  {
    size_t nLocalIds;
    const gid_t *gids;
    const gid_t *colIds;
    const lno_t *offsets;

    weightDim_ = 0;    // TODO not implemented yet

    try{
      nLocalIds = ia->getRowListView(gids, offsets, colIds);
    }
    Z2_FORWARD_EXCEPTIONS;

    if (nLocalIds){
      gids_ = arcp(const_cast<gid_t *>(gids), 0, nLocalIds, false);
    }

    RCP<const idmap_t> idMap;

    try{
      idMap = rcp(new idmap_t(env_, gids_, gnosMustBeConsecutive));
    }
    Z2_FORWARD_EXCEPTIONS;

    gnosAreGids_ = idMap->gnosAreGids();

    this->setIdentifierMap(idMap);   // Base Model method

    gno_t lsum = nLocalIds;
    reduceAll<int, gno_t>(*comm_, Teuchos::REDUCE_SUM, 1, &lsum,
      &numGlobalIdentifiers_);

    if (!gnosAreGids_ && nLocalIds>0){
      gno_t *tmpGno = new gno_t [nLocalIds];
      Z2_LOCAL_MEMORY_ASSERTION(*env_, nLocalIds, tmpGno);
      gnos_ = arcp(tmpGno, 0, gids_.size(), true);

      try{
        ArrayRCP<gid_t> gidsNonConst = arcp_const_cast<gid_t>(gids_);
        idMap->gidTranslate(gidsNonConst(0, nLocalIds),  
          gnos_(0, nLocalIds), TRANSLATE_APP_TO_LIB);
      }
      Z2_FORWARD_EXCEPTIONS;
    }
   
    gnosConst_ = arcp_const_cast<const gno_t>(gnos_);
  }

  /*! Returns the number identifierson this process.
   */
  size_t getLocalNumIdentifiers() const { return gids_.size(); }

  /*! Returns the global number identifiers.
   */
  global_size_t getGlobalNumIdentifiers() const {return numGlobalIdentifiers_;}

  /*! Returns the dimension (0 or greater) of identifier weights.
   */
  int getIdentifierWeightDim() const { return weightDim_; }

  /*! Sets pointers to this process' identifier Ids and their weights.
      \param Ids will on return point to the list of the global Ids for
        each identifier on this process.
      \param wgts will on return point to a list of the weight or weights
         associated with each identifier in the Ids list.  Weights are listed by
         identifier by weight component.
       \return The number of ids in the Ids list.
   */

  size_t getIdentifierList( ArrayView<const gno_t> &Ids,
    ArrayView<const scalar_t> &wgts) const 
  {
    size_t n = getLocalNumIdentifiers();
    size_t nweights = n * weightDim_;

    Ids = ArrayView<const gno_t>(Teuchos::null);
    wgts = ArrayView<const scalar_t>(Teuchos::null);

    if (n){
      if (gnosAreGids_)
        Ids = gids_(0, n);
      else
        Ids = gnosConst_(0, n);

      if (nweights)
        wgts = weights_(0, nweights);
    }

    return n;
  }

  ////////////////////////////////////////////////////
  // The Model interface.
  ////////////////////////////////////////////////////

  size_t getLocalNumObjects() const
  {
    return getLocalNumIdentifiers();
  }

  size_t getGlobalNumObjects() const
  {
    return getGlobalNumIdentifiers();
  }

  void getGlobalObjectIds(ArrayView<const gno_t> &gnos) const 
  { 
    ArrayView<const scalar_t> weights;
    getIdentifierList(gnos, weights);
  }

private:

  bool gnosAreGids_;
  int weightDim_;
  gno_t numGlobalIdentifiers_;
  const RCP<const Environment> env_;
  const RCP<const Comm<int> > comm_;
  ArrayRCP<const gid_t> gids_;
  ArrayRCP<const scalar_t> weights_;
  ArrayRCP<gno_t> gnos_;
  ArrayRCP<const gno_t> gnosConst_;
};

}  // namespace Zoltan2

#endif
