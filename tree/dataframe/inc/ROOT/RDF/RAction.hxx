// Author: Enrico Guiraud, Danilo Piparo CERN  09/2018

/*************************************************************************
 * Copyright (C) 1995-2018, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_RACTION
#define ROOT_RACTION

#include "ROOT/RDF/GraphNode.hxx"
#include "ROOT/RDF/RActionBase.hxx"
#include "ROOT/RDF/NodesUtils.hxx" // InitRDFValues
#include "ROOT/RDF/Utils.hxx"      // ColumnNames_t
#include "ROOT/RDF/RColumnValue.hxx"

#include <memory>
#include <string>
#include <vector>

namespace ROOT {
namespace Internal {
namespace RDF {

namespace RDFDetail = ROOT::Detail::RDF;
namespace RDFGraphDrawing = ROOT::Internal::RDF::GraphDrawing;

// fwd declarations for RActionCRTP
namespace GraphDrawing {
std::shared_ptr<GraphNode> CreateDefineNode(const std::string &colName, const RDFDetail::RCustomColumnBase *columnPtr);
bool CheckIfDefaultOrDSColumn(const std::string &name, const std::shared_ptr<RDFDetail::RCustomColumnBase> &column);
} // ns GraphDrawing

/// Unused, not instantiatable. Only the partial specialization RActionCRTP<RAction<...>> can be used.
template <typename Dummy>
class RActionCRTP {
   static_assert(sizeof(Dummy) < 0, "The unspecialized version of RActionCRTP should never be instantiated");
};

// fwd decl for RActionCRTP
template <typename Helper, typename PrevDataFrame, typename ColumnTypes_t>
class RAction;

/// A common template base class for all RActions. Avoids code repetition for specializations of RActions
/// for different helpers, implementing all of the common logic.
template <typename Helper, typename PrevDataFrame, typename ColumnTypes_t>
class RActionCRTP<RAction<Helper, PrevDataFrame, ColumnTypes_t>> : public RActionBase {
   using Action_t = RAction<Helper, PrevDataFrame, ColumnTypes_t>;

   Helper fHelper;
   const std::shared_ptr<PrevDataFrame> fPrevDataPtr;
   PrevDataFrame &fPrevData;

public:
   using TypeInd_t = std::make_index_sequence<ColumnTypes_t::list_size>;

   RActionCRTP(Helper &&h, const ColumnNames_t &bl, std::shared_ptr<PrevDataFrame> pd,
               const RBookedCustomColumns &customColumns)
      : RActionBase(pd->GetLoopManagerUnchecked(), bl, customColumns), fHelper(std::forward<Helper>(h)),
        fPrevDataPtr(std::move(pd)), fPrevData(*fPrevDataPtr) { }

   RActionCRTP(const RActionCRTP &) = delete;
   RActionCRTP &operator=(const RActionCRTP &) = delete;

   Helper &GetHelper() { return fHelper; }

   void Initialize() final { fHelper.Initialize(); }

   void InitSlot(TTreeReader *r, unsigned int slot) final
   {
      for (auto &bookedBranch : GetCustomColumns().GetColumns())
         bookedBranch.second->InitSlot(r, slot);
      static_cast<Action_t*>(this)->InitColumnValues(r, slot);
      fHelper.InitTask(r, slot);
   }

   void Run(unsigned int slot, Long64_t entry) final
   {
      // check if entry passes all filters
      if (fPrevData.CheckFilters(slot, entry))
         static_cast<Action_t *>(this)->Exec(slot, entry, TypeInd_t());
   }

   void TriggerChildrenCount() final { fPrevData.IncrChildrenCount(); }

   void FinalizeSlot(unsigned int slot) final
   {
      ClearValueReaders(slot);
      for (auto &column : GetCustomColumns().GetColumns()) {
         column.second->ClearValueReaders(slot);
      }
      fHelper.CallFinalizeTask(slot);
   }

   void ClearValueReaders(unsigned int slot) { static_cast<Action_t *>(this)->ResetColumnValues(slot, TypeInd_t()); }

   void Finalize() final
   {
      fHelper.Finalize();
      SetHasRun();
   }

   std::shared_ptr<RDFGraphDrawing::GraphNode> GetGraph()
   {
      auto prevNode = fPrevData.GetGraph();
      auto prevColumns = prevNode->GetDefinedColumns();

      // Action nodes do not need to ask an helper to create the graph nodes. They are never common nodes between
      // multiple branches
      auto thisNode = std::make_shared<RDFGraphDrawing::GraphNode>(fHelper.GetActionName());
      auto evaluatedNode = thisNode;
      for (auto &column : GetCustomColumns().GetColumns()) {
         /* Each column that this node has but the previous hadn't has been defined in between,
          * so it has to be built and appended. */
         if (RDFGraphDrawing::CheckIfDefaultOrDSColumn(column.first, column.second))
            continue;
         if (std::find(prevColumns.begin(), prevColumns.end(), column.first) == prevColumns.end()) {
            auto defineNode = RDFGraphDrawing::CreateDefineNode(column.first, column.second.get());
            evaluatedNode->SetPrevNode(defineNode);
            evaluatedNode = defineNode;
         }
      }

      thisNode->AddDefinedColumns(GetCustomColumns().GetNames());
      thisNode->SetAction(HasRun());
      evaluatedNode->SetPrevNode(prevNode);
      return thisNode;
   }

   /// This method is invoked to update a partial result during the event loop, right before passing the result to a
   /// user-defined callback registered via RResultPtr::RegisterCallback
   void *PartialUpdate(unsigned int slot) final { return PartialUpdateImpl(slot); }

private:
   // this overload is SFINAE'd out if Helper does not implement `PartialUpdate`
   // the template parameter is required to defer instantiation of the method to SFINAE time
   template <typename H = Helper>
   auto PartialUpdateImpl(unsigned int slot) -> decltype(std::declval<H>().PartialUpdate(slot), (void *)(nullptr))
   {
      return &fHelper.PartialUpdate(slot);
   }

   // this one is always available but has lower precedence thanks to `...`
   void *PartialUpdateImpl(...) { throw std::runtime_error("This action does not support callbacks!"); }
};

/// An action node in a RDF computation graph.
template <typename Helper, typename PrevDataFrame, typename ColumnTypes_t = typename Helper::ColumnTypes_t>
class RAction final : public RActionCRTP<RAction<Helper, PrevDataFrame, ColumnTypes_t>> {
   std::vector<RDFValueTuple_t<ColumnTypes_t>> fValues;

public:
   using ActionCRTP_t = RActionCRTP<RAction<Helper, PrevDataFrame, ColumnTypes_t>>;

   RAction(Helper &&h, const ColumnNames_t &bl, std::shared_ptr<PrevDataFrame> pd,
           const RBookedCustomColumns &customColumns)
      : ActionCRTP_t(std::forward<Helper>(h), bl, std::move(pd), customColumns), fValues(GetNSlots()) { }

   void InitColumnValues(TTreeReader *r, unsigned int slot)
   {
      InitRDFValues(slot, fValues[slot], r, RActionBase::GetColumnNames(), RActionBase::GetCustomColumns(),
                    typename ActionCRTP_t::TypeInd_t{});
   }

   template <std::size_t... S>
   void Exec(unsigned int slot, Long64_t entry, std::index_sequence<S...>)
   {
      (void)entry; // avoid bogus 'unused parameter' warning in gcc4.9
      ActionCRTP_t::GetHelper().Exec(slot, std::get<S>(fValues[slot]).Get(entry)...);
   }

   template <std::size_t... S>
   void ResetColumnValues(unsigned int slot, std::index_sequence<S...> s)
   {
      ResetRDFValueTuple(fValues[slot], s);
   }
};

} // ns RDF
} // ns Internal
} // ns ROOT

#endif // ROOT_RACTION