#include "core/reference.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <set>

namespace seethis::core {
namespace {
double DistanceToSegment(Point2D p,Point2D a,Point2D b) {
  const double dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
  const double t=length>0?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/length,0.0,1.0):0;
  return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);
}
bool HitPath(const Context& context,const std::vector<DisplayPoint>& path,
             DisplayPoint point,double radius,
             const std::vector<FrozenDisplay>& displays) {
  if(!DisplayTopologyMatches(context.displays,displays))return false;
  for(std::size_t index=0;index<path.size();++index) {
    const auto& sample=path[index];
    if(sample.display_id!=point.display_id||sample.backing_scale!=point.backing_scale)continue;
    const auto& previous=index>0&&path[index-1].display_id==sample.display_id?
        path[index-1]:sample;
    if(DistanceToSegment(point.position,previous.position,sample.position)<=radius)return true;
  }return false;
}
bool HitRegions(const Reference& reference,DisplayPoint point,double radius,
                const std::vector<FrozenDisplay>& displays) {
  for(const auto& region:EffectiveRegions(reference)) {
    bool hit = false;
    ForEachSubpath(region, [&](const auto& path) {
      if (HitPath(reference.context,path,point,radius,displays)) hit = true;
    });
    if (hit) return true;
  }
  return false;
}
ReferenceMark LightweightMark(const Reference& source) {
  ReferenceMark mark{source};mark.value.source.bytes.clear();
  mark.value.crop.bytes.clear();return mark;
}
bool VisibleJob(const ReferenceJobSnapshot& job,
                const WindowObservation& window_observation,
                const PageObservation& page_observation) {
  return ReferenceVisible(job.context,job.page_identity,window_observation,
                          page_observation);
}
}  // namespace

struct MarkController::Transaction {
  std::uint64_t generation=0;
  Reference reference;
  std::optional<CaptureResult> capture;
  std::string capture_error;
  Stamp began;
  // Technical waits are independent of the user's renewable interaction idle
  // clock and of immutable capture provenance.
  std::int64_t capture_deadline_us=0,release_deadline_us=0,append_deadline_us=0;
  std::int64_t key_down_monotonic_ms=0;
  std::int64_t key_up_monotonic_ms=0;
  std::uint64_t copy_generation=0;
  std::optional<std::int64_t> pasteboard_change_count;
  bool released=false;
  bool submitted=false,status_action_only=false;
  std::string append_root;
  std::uint64_t expected_revision=0,input_epoch=0;
  bool input_healthy=false,ordinary_source_authorized=false;
  std::uint64_t presentation_generation=0;
  std::optional<ReferenceShot> single_checkpoint;
  WindowObservation start_window;
  PageObservation start_page;
  // Shared with a queued main-thread completion; false can never become true.
  std::shared_ptr<std::atomic_bool> context_continuous=std::make_shared<std::atomic_bool>(true);
  // Positive coordinate evidence can precede persistence/queued completion.
  // Zero is no loss; other values encode SavedCoordinateLoss + 1.
  std::shared_ptr<std::atomic<int>> coordinate_loss=std::make_shared<std::atomic<int>>(0);
};

MarkController::MarkController(ReferenceStore& store,Clipboard clipboard,Clock clock)
    :store_(store),clipboard_(std::move(clipboard)),
     dispatcher_([](std::function<void()> task){task();}),clock_(clock?std::move(clock):Clock{Now}) {}

MarkController::MarkController(ReferenceStore& store,
    PendingClipboard pending_clipboard,ReadyClipboard ready_clipboard,
    Dispatcher dispatcher,Clock clock)
    :store_(store),pending_clipboard_(std::move(pending_clipboard)),
     ready_clipboard_(std::move(ready_clipboard)),dispatcher_(std::move(dispatcher)),
     clock_(clock?std::move(clock):Clock{Now}) {}

MarkController::~MarkController(){lifetime_.reset();}

std::uint64_t MarkController::Begin(std::string id,double crop_margin,
                                    std::int64_t key_down_monotonic_ms) {
  if(current_)Abort("superseded drawing attempt");
  if(std::any_of(accepted_.begin(),accepted_.end(),[](const auto& transaction) {return transaction->status_action_only;})) {
    // One snapshot prunes the store once, independent of retained custody count.
    std::set<std::string> failed;
    for(const auto& job:store_.MetadataJobs())if(job.state==ReferenceJobState::kFailed)failed.insert(job.id);
    std::erase_if(accepted_,[&](const auto& transaction) {
      return transaction->status_action_only&&!failed.contains(transaction->reference.id);
    });
  }
  ++generation_;current_=std::make_unique<Transaction>();
  current_->generation=generation_;current_->reference.id=id;
  current_->reference.mark_id=std::move(id);current_->reference.crop_margin=crop_margin;
  current_->began=clock_();current_->reference.operation_started=current_->began;
  current_->capture_deadline_us=current_->began.monotonic_us+30'000'000;
  current_->key_down_monotonic_ms=key_down_monotonic_ms;
  current_->input_epoch=input_epoch_;current_->input_healthy=input_healthy_;
  current_->presentation_generation=presentation_generation_;
  current_->start_window=window_observation_;current_->start_page=page_observation_;
  phase_=ReferencePhase::kContextPending;copy_result_=CopyResult::kNone;
  last_copy_id_.clear();
  status_="Capturing original window…";return generation_;
}

std::uint64_t MarkController::BeginAppend(std::string root,std::string mark,
    std::uint64_t expected,std::string operation,double margin,std::int64_t key_down) {
  const auto notes=store_.MarkSnapshots(root);
  if(notes.status!=MarkNotesStatus::kReady||std::none_of(notes.marks.begin(),notes.marks.end(),
      [&](const auto& value){return value.id==mark;})) {
    status_="Append target unavailable";return 0;
  }
  return BeginAppendReference(std::move(root),expected,std::move(operation),margin,key_down);
}
std::uint64_t MarkController::BeginAppendReference(std::string root,
    std::uint64_t expected,std::string operation,double margin,std::int64_t key_down) {
  if(std::count_if(accepted_.begin(),accepted_.end(),[](const auto& transaction) {return !transaction->status_action_only;})>=ReferenceStore::kMaximumUnfinishedJobs) {status_="Append busy; wait for capture completion";return 0;}
  const auto collection=store_.Collection(root);
  if(collection.status!=MarkNotesStatus::kReady||collection.revision!=expected) {
    status_="Append reference unavailable or changed";return 0;
  }
  PrunePresentations();
  const auto generation=Begin(std::move(operation),margin,key_down);
  current_->append_root=root;current_->expected_revision=expected;
  current_->input_epoch=input_epoch_;current_->input_healthy=input_healthy_;
  current_->presentation_generation=presentation_generation_;
  current_->start_window=window_observation_;current_->start_page=page_observation_;
  // Legacy transient capture witness only. Saved single/multi projection is
  // store-backed and never requires this paint checkpoint.
  if(!collection.multi())for(const auto& entry:presented_singles_)
    if(entry.epoch==input_epoch_&&entry.shot.metadata->id==root&&
       entry.shot.id==root&&entry.shot.metadata->source.sha256==collection.shots.front().metadata->source.sha256)
      current_->single_checkpoint=entry.shot;
  return generation;
}
std::optional<OwnUIPresentationSourceTarget> MarkController::own_ui_presentation_source() const {
  return own_ui_source_;
}
std::optional<OwnUIPresentationSourceTarget> MarkController::BeginOwnUIPresentationSource() {
  PrunePresentations();
  if(own_ui_source_)return own_ui_source_;
  if(!input_healthy_||!presentation_permitted_)return {};
  if(own_input_focus_&&own_ui_query_identity_) {
    const auto identity=*own_ui_query_identity_;
    for(const auto& job:store_.MetadataJobs()) {
      if(job.state!=ReferenceJobState::kReady)continue;
      const auto collection=store_.Collection(job.id);if(collection.status!=MarkNotesStatus::kReady)continue;
      for(const auto& shot:collection.shots) {
        const auto& ref=*shot.metadata;const auto state=SavedProjection(ref);
        const auto& a=ref.context;const auto& b=identity.context;
        const bool page=(!ref.page_identity&&!identity.page)||(ref.page_identity&&identity.page&&
            ref.page_identity->opaque_tab_id==identity.page->opaque_tab_id&&
            ref.page_identity->navigation_digest==identity.page->navigation_digest);
        if(state!=SavedProjectionState::kTerminal&&state!=SavedProjectionState::kCoordinateInvalid&&page&&
           a.scope==b.scope&&a.pid==b.pid&&a.window_id==b.window_id&&a.bundle_id==b.bundle_id&&a.window_title==b.window_title&&
           (a.scope.kind==CaptureScopeKind::kApplication||
            ((a.native_window.version==1||a.native_window.version==2)&&a.native_window.process_start_identity_us==b.native_window.process_start_identity_us&&
             a.native_window.observation_session==b.native_window.observation_session&&a.native_window.incarnation==b.native_window.incarnation))) {
          own_ui_source_=OwnUIPresentationSourceTarget{++own_ui_generation_,input_epoch_,a,ref.page_identity};
          return own_ui_source_; // No observation/timestamp is carried or created.
        }
      }
    }
  }
  for(const auto& job:store_.MetadataJobs()) {
    if(job.state!=ReferenceJobState::kReady)continue;
    const auto collection=store_.Collection(job.id);
    if(collection.status!=MarkNotesStatus::kReady)continue;
    for(const auto& shot:collection.shots) {
      const auto& ref=*shot.metadata;
      if((ref.context.native_window.version==1||ref.context.native_window.version==2||ref.context.scope.kind==CaptureScopeKind::kApplication)&&PresentedReferenceVisible(ref)&&
         ReferenceVisible(ref.context,ref.page_identity,window_observation_,page_observation_)) {
        own_ui_source_=OwnUIPresentationSourceTarget{++own_ui_generation_,input_epoch_,ref.context,ref.page_identity};break;
      }
    }
    if(own_ui_source_)break;
  }
  if(own_ui_source_) {
    // Carry the existing fresh evidence without changing its timestamp. This
    // bridges activation only until the first genuinely new native observation.
    own_ui_window_=window_observation_;own_ui_page_=page_observation_;
  }
  return own_ui_source_;
}
void MarkController::EndOwnUIPresentationSource() {
  own_ui_source_.reset();own_ui_query_identity_.reset();own_ui_window_={};own_ui_page_={};++own_ui_generation_;
}
bool MarkController::PublishOwnUIPresentationObservation(
    const OwnUIPresentationSourceTarget& target,WindowObservation window,PageObservation page) {
  if(!own_ui_source_||target.generation!=own_ui_source_->generation)return false;
  const auto& required=*own_ui_source_;
  // Rejected envelopes have no authority to mutate saved coordinates. In
  // particular, a late callback must not turn stale bounds into a permanent ban.
  if(target.epoch!=required.epoch||
     window.generation<=own_ui_window_.generation||
     window.observed.monotonic_us<=own_ui_window_.observed.monotonic_us||
     (required.page&&(page.generation<=own_ui_page_.generation||
       page.observed.monotonic_us<=own_ui_page_.observed.monotonic_us)))return false;
  if(!own_input_focus_||!input_healthy_||!presentation_permitted_)return false;
  // A callback for this request cannot classify geometry belonging to another
  // source. Bounds and native lifetime may change, so they are judged only
  // after the requested pid/bundle/window and observer session are associated.
  const auto& context=required.context;
  const auto& native=WindowIdentityForContext(context,window);
  const bool requestedSource=context.scope.kind==CaptureScopeKind::kApplication?
      window.application_available&&window.pid==context.pid&&window.bundle_id==context.bundle_id&&
          ApplicationIdentityForContext(context,window)==context.scope.process_start_identity_us:
      window.pid==context.window_pid&&window.bundle_id==context.bundle_id&&window.window_id==context.window_id&&
      native.version==context.native_window.version&&native.observation_session==context.native_window.observation_session;
  if(!requestedSource){RetireLiveShot();return false;}
  ObserveSavedWindowGeometry(window);
  const bool valid=
      (context.scope.kind==CaptureScopeKind::kApplication||
       (native.observation_session==required.context.native_window.observation_session&&
        native.incarnation==required.context.native_window.incarnation))&&
      ReferenceVisible(required.context,required.page,window,page);
  if(!valid){RetireLiveShot();return false;}
  own_ui_window_=std::move(window);own_ui_page_=std::move(page);
  presentation_state_.reset();
  PrunePresentations();return own_ui_source_.has_value();
}
namespace {
void RecordCoordinateLoss(const std::shared_ptr<std::atomic<int>>& loss,SavedCoordinateLoss reason) {
  if(reason==SavedCoordinateLoss::kNativeLifetime)loss->store(static_cast<int>(reason)+1);
  else {int empty=0;(void)loss->compare_exchange_strong(empty,static_cast<int>(reason)+1);}
}
bool PositiveRect(Rect r) {
  return std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.width)&&std::isfinite(r.height)&&std::isfinite(r.x+r.width)&&std::isfinite(r.y+r.height)&&r.width>0&&r.height>0;
}
bool SameSavedSource(const Reference& a,const Reference& b) {
  const auto& x=a.context;const auto& y=b.context;
  const bool page=(!a.page_identity&&!b.page_identity)||(a.page_identity&&b.page_identity&&
      a.page_identity->provider==b.page_identity->provider&&a.page_identity->provider_version==b.page_identity->provider_version&&
      a.page_identity->bundle_id==b.page_identity->bundle_id&&a.page_identity->browser_pid==b.page_identity->browser_pid&&
      a.page_identity->window_id==b.page_identity->window_id&&a.page_identity->process_start_identity_us==b.page_identity->process_start_identity_us&&
      a.page_identity->opaque_tab_id==b.page_identity->opaque_tab_id&&a.page_identity->navigation_digest==b.page_identity->navigation_digest);
  return page&&x.scope==y.scope&&x.pid==y.pid&&x.window_id==y.window_id&&x.bundle_id==y.bundle_id&&x.window_title==y.window_title&&
      x.window.x==y.window.x&&x.window.y==y.window.y&&x.window.width==y.window.width&&x.window.height==y.window.height&&
      x.native_window.version==y.native_window.version&&x.native_window.process_start_identity_us==y.native_window.process_start_identity_us&&
      x.native_window.observation_session==y.native_window.observation_session&&x.native_window.incarnation==y.native_window.incarnation&&
      DisplayTopologyMatches(x.displays,y.displays);
}
bool KnownTopology(const std::vector<FrozenDisplay>& displays) {
  std::set<DisplayId> ids;std::set<std::string> names;
  if(displays.empty())return false;
  for(const auto& d:displays)if(!d.id||d.uuid.empty()||!ids.insert(d.id).second||!names.insert(d.uuid).second||
      !PositiveRect(d.logical)||!PositiveRect(d.pixels)||!std::isfinite(d.scale)||d.scale<=0||
      !std::isfinite(d.logical_to_pixels.a)||!std::isfinite(d.logical_to_pixels.d)||
      !std::isfinite(d.logical_to_pixels.tx)||!std::isfinite(d.logical_to_pixels.ty)||
      d.pixels.width!=d.logical.width*d.scale||d.pixels.height!=d.logical.height*d.scale||
      d.logical_to_pixels.a!=d.scale||d.logical_to_pixels.d!=-d.scale||
      d.logical_to_pixels.tx!=d.pixels.x-d.logical.x*d.scale||
      d.logical_to_pixels.ty!=d.pixels.y+(d.logical.y+d.logical.height)*d.scale)return false;
  return true;
}
}
SavedProjectionState MarkController::SavedProjection(const Reference& ref) const {
  const auto entry=store_.ReadPresentation(ref.id);
  if(!entry||entry->job.state!=ReferenceJobState::kReady)return SavedProjectionState::kTerminal;
  const auto& collection=entry->collection;
  if(collection.status!=MarkNotesStatus::kReady||entry->notes.status!=MarkNotesStatus::kReady)return SavedProjectionState::kTerminal;
  const auto membership=entry->source_membership.find(std::to_string(ref.first_mark_slot)+":"+ref.source.sha256);
  if(membership==entry->source_membership.end())return SavedProjectionState::kTerminal;
  const auto* member=&collection.shots[membership->second];
  if(!member->metadata||!SameSavedSource(ref,*member->metadata))return SavedProjectionState::kTerminal;
  for(const auto& blocked:coordinate_exclusions_)if(blocked.root==ref.id&&blocked.shot==member->id&&blocked.source_sha256==ref.source.sha256)
    return blocked.reason==SavedCoordinateLoss::kNativeLifetime?SavedProjectionState::kTerminal:SavedProjectionState::kCoordinateInvalid;
  // A successful store publication may precede its dispatched completion.
  // The operation's exact-source loss tag already gates that newly saved shot.
  const auto pendingLoss=[&](const auto& transaction)->std::optional<SavedCoordinateLoss> {
    if(transaction&&transaction->coordinate_loss->load()&&transaction->reference.id==member->id&&
       (transaction->append_root.empty()?transaction->reference.id:transaction->append_root)==ref.id&&
       SameSavedSource(transaction->reference,ref))return static_cast<SavedCoordinateLoss>(transaction->coordinate_loss->load()-1);
    return {};
  };
  auto loss=pendingLoss(current_);
  if(!loss)for(const auto& transaction:accepted_)if((loss=pendingLoss(transaction)))break;
  if(loss)return *loss==SavedCoordinateLoss::kNativeLifetime?SavedProjectionState::kTerminal:SavedProjectionState::kCoordinateInvalid;
  if(!input_healthy_||!presentation_permitted_||
     (ref.context.scope.kind==CaptureScopeKind::kApplication&&!display_observation_)||
     (display_observation_&&(!KnownTopology(*display_observation_)||
      !DisplayTopologyMatches(ref.context.displays,*display_observation_))))return SavedProjectionState::kTemporarilyHidden;
  if(ReferenceVisible(ref.context,ref.page_identity,window_observation_,page_observation_))return SavedProjectionState::kVisible;
  if(!own_ui_source_)return SavedProjectionState::kTemporarilyHidden;
  if(!own_input_focus_&&!WindowObservationMatchesContext(own_ui_source_->context,window_observation_))return SavedProjectionState::kTemporarilyHidden;
  return ReferenceVisible(ref.context,ref.page_identity,own_ui_window_,own_ui_page_)&&
      ReferenceVisible(own_ui_source_->context,own_ui_source_->page,own_ui_window_,own_ui_page_)?
      SavedProjectionState::kVisible:SavedProjectionState::kTemporarilyHidden;
}
bool MarkController::PresentedReferenceVisible(const Reference& ref) const {
  return SavedProjection(ref)==SavedProjectionState::kVisible;
}
void MarkController::ExcludeSavedShot(const ReferenceShot& shot,SavedCoordinateLoss reason) {
  if(!shot.metadata)return;
  const auto& ref=*shot.metadata;
  for(auto& existing:coordinate_exclusions_)if(existing.root==ref.id&&existing.shot==shot.id&&existing.source_sha256==ref.source.sha256) {
    if(reason==SavedCoordinateLoss::kNativeLifetime)existing.reason=reason;
    return;
  }
  coordinate_exclusions_.push_back({ref.id,shot.id,ref.source.sha256,reason});
}
void MarkController::SetDisplayObservation(std::vector<FrozenDisplay> displays) {
  // Geometry describes current display eligibility, not permanent loss of the
  // saved screen coordinates. Missing/mismatched topology hides until an exact
  // fresh original topology is available again.
  display_observation_=std::move(displays);presentation_state_.reset();
}
void MarkController::ObserveSavedWindowGeometry(const WindowObservation& observation) {
  const auto age=Now().monotonic_us-observation.observed.monotonic_us;
  if(observation.availability!=WindowAvailability::kReady||!observation.generation||
     observation.observed.monotonic_us<=0||age<0||age>2'000'000||!PositiveRect(observation.bounds))return;
  const auto lossFor=[&](const Context& context)->std::optional<SavedCoordinateLoss> {
    if(context.scope.kind==CaptureScopeKind::kApplication || context.window_pid!=observation.pid)return {};
    const auto& old=context.native_window;const auto& fresh=WindowIdentityForContext(context,observation);
    if(old.version==1||old.version==2) {
      if(fresh.version!=old.version||fresh.process_start_identity_us<=0||fresh.process_start_identity_us>observation.observed.utc_us||
         fresh.observation_session.empty()||!fresh.incarnation)return {};
      if(old.process_start_identity_us!=fresh.process_start_identity_us||
         (context.window_id==observation.window_id&&old.observation_session==fresh.observation_session&&old.incarnation!=fresh.incarnation))
        return SavedCoordinateLoss::kNativeLifetime;
    } else if(old.version!=0)return {};
    // Window bounds mismatches remain a current-observation hide. Only a
    // positively observed native lifetime replacement is irreversible.
    return {};
  };
  for(const auto& saved:store_.ReadPresentation()->jobs)if(saved->job.state==ReferenceJobState::kReady) {
    if(saved->collection.status!=MarkNotesStatus::kReady)continue;
    for(const auto& shot:saved->collection.shots)if(const auto loss=lossFor(shot.metadata->context))ExcludeSavedShot(shot,*loss);
  }
  const auto pending=[&](const auto& transaction) {
    if(transaction)if(const auto loss=lossFor(transaction->reference.context))
      RecordCoordinateLoss(transaction->coordinate_loss,*loss);
  };
  pending(current_);for(const auto& transaction:accepted_)pending(transaction);
}
void MarkController::SetViewportContinuity(std::uint64_t epoch,bool healthy) {
  if(!healthy)RetireLiveShot();
  else if(epoch!=input_epoch_)RetireCaptureWitnesses();
  input_epoch_=epoch;input_healthy_=healthy;
}
void MarkController::RetireCaptureWitnesses() {
  // Raw input continuity gates capture attempts, not an independently fresh
  // saved-source observation. Scrolling never changes saved screen geometry.
  admitted_.clear();fresh_singles_.clear();presented_singles_.clear();presentation_prefix_=false;++presentation_generation_;
}
void MarkController::RetireLiveShot() {
  const auto identity=own_input_focus_?(own_ui_source_?own_ui_source_:own_ui_query_identity_):std::nullopt;
  EndOwnUIPresentationSource();own_ui_query_identity_=identity;
  RetireCaptureWitnesses();
}
void MarkController::RetireReferencePresentation(std::string_view root) {
  const auto remove=[&](auto& entries){std::erase_if(entries,[&](const auto& entry){return entry.shot.metadata->id==root;});};
  remove(admitted_);remove(fresh_singles_);remove(presented_singles_);
  const auto poison=[&](const auto& attempt) {
    if(attempt&&(attempt->append_root==root||attempt->reference.id==root))attempt->context_continuous->store(false);
  };
  poison(current_);for(const auto& attempt:accepted_)poison(attempt);
}
void MarkController::SetPresentationPermitted(bool permitted) {
  if(!permitted)RetireLiveShot();
  presentation_permitted_=permitted;
}
bool MarkController::BeginPresentationPrefix() {
  if(!input_healthy_||!presentation_permitted_||
     window_observation_.availability!=WindowAvailability::kReady ||
     (window_observation_.bundle_id=="com.google.Chrome"&&
      page_observation_.availability!=PageAvailability::kReady))return false;
  PrunePresentations();presentation_prefix_=true;return true;
}
bool MarkController::ConfirmPresentationPrefix() {
  if(!presentation_prefix_)return false;
  presentation_prefix_=false;return true;
}
void MarkController::CancelPresentationPrefix() {
  if(!presentation_prefix_)return;
  presentation_prefix_=false;
  // The provisional shortcut prefix is not a source/viewport invalidation.
  // Keep only existing continuously valid membership; pruning cannot re-admit.
  PrunePresentations();
}
void MarkController::RecordPresentedSingle(const Reference& reference,
    const std::vector<FrozenDisplay>& displays) {
  if(presentation_prefix_||!input_healthy_||!presentation_permitted_||
     !DisplayTopologyMatches(reference.context.displays,displays)||
     !ReferenceVisible(reference.context,reference.page_identity,window_observation_,page_observation_))return;
  const auto witness=std::find_if(fresh_singles_.begin(),fresh_singles_.end(),[&](const auto& entry) {
    return entry.epoch==input_epoch_&&entry.shot.metadata->id==reference.id&&
        entry.shot.metadata->source.sha256==reference.source.sha256;
  });
  if(witness==fresh_singles_.end())return; // Repaint/history cannot manufacture fresh lineage.
  const auto entry=store_.ReadPresentation(reference.id);if(!entry)return;
  const auto& collection=entry->collection;
  if(collection.status!=MarkNotesStatus::kReady||collection.multi()||collection.shots.empty()||
     collection.shots.front().metadata->source.sha256!=reference.source.sha256)return;
  std::erase_if(presented_singles_,[&](const auto& entry){return entry.shot.metadata->id==reference.id;});
  if(presented_singles_.size()<500)presented_singles_.push_back({collection.shots.front(),input_epoch_});
}
std::vector<ReferenceShot> MarkController::LiveShots(std::string_view root) const {
  std::vector<ReferenceShot> result;
  PrepareCurrentPresentation();
  const auto candidates=presentation_shots_.find(std::string(root));
  if(candidates==presentation_shots_.end())return result;
  for(const auto& shot:candidates->second)if(shot.metadata&&PresentedReferenceVisible(*shot.metadata))result.push_back(shot);
  return result;
}
bool MarkController::IsShotAdmitted(std::string_view root,std::string_view shot) const {
  if(presentation_prefix_)return false;
  const auto current=LiveShots(root);
  return std::any_of(current.begin(),current.end(),[&](const auto& value){return value.id==shot;});
}
std::optional<ReferenceShot> MarkController::LiveShot(std::string_view root) const {
  if(presentation_prefix_)return {};
  auto shots=LiveShots(root);if(shots.empty())return {};return shots.back();
}
void MarkController::PreparePresentationObservation() {
  // Judge the evidence that kept admission continuous BEFORE replacing it.
  // A healthy private source bridges a real foreground handoff; stale private
  // or ordinary evidence cannot be made continuous by a new matching sample.
  if(own_ui_source_) {
    if(!input_healthy_||!presentation_permitted_||
       !ReferenceVisible(own_ui_source_->context,own_ui_source_->page,own_ui_window_,own_ui_page_))RetireLiveShot();
  } else PrunePresentations();
}
void MarkController::PrunePresentations() {
  bool invalid=false;
  const auto prune=[&](auto& entries) {
    std::erase_if(entries,[&](const auto& entry) {
      const auto& reference=*entry.shot.metadata;
      const auto saved=store_.ReadPresentation(reference.id);
      const bool remove=entry.epoch!=input_epoch_||!saved||saved->job.state!=ReferenceJobState::kReady||
          saved->collection.status!=MarkNotesStatus::kReady||
          !PresentedReferenceVisible(reference);
      invalid=invalid||remove;return remove;
    });
  };
  prune(admitted_);prune(fresh_singles_);prune(presented_singles_);
  if(invalid){if(presentation_prefix_)RetireLiveShot();else ++presentation_generation_;}
  // These arrays are transient capture witnesses only. Their pruning cannot
  // revoke saved projection or the independently bound source query.

}

bool MarkController::Current(std::uint64_t generation) const {
  return current_&&current_->generation==generation;
}

bool MarkController::Pending() const {return current_!=nullptr;}

void MarkController::BindCaptureDisplay(std::uint64_t generation,DisplayId id) {
  if(Current(generation))current_->reference.capture_display_id=id;
}
bool MarkController::BindContext(std::uint64_t generation,Context context) {
  if(!Current(generation)||current_->reference.context.pid!=0)return false;
  const bool app=context.scope.kind==CaptureScopeKind::kApplication;
  if(!ValidCaptureScope(context)||context.pid<=0||context.pid==context.self_pid||
     (!app&&(context.pid!=context.window_pid||context.window_id==0))||context.displays.empty()) {
    current_->capture_error="context_unavailable";status_="Original window context unavailable";
    return false;
  }
  if(context.bundle_id=="com.google.Chrome")context.window_title.clear();
  if(context.scope.kind==CaptureScopeKind::kPage) {
    if(!page_observation_.identity || !PageIdentityVisible(page_observation_.identity,page_observation_) ||
       !PageIdentityMatchesContext(*page_observation_.identity,context))return false;
    current_->reference.page_identity=page_observation_.identity;
    current_->reference.captured_page=page_observation_.captured_page;
  }
  current_->reference.context=std::move(context);
  current_->ordinary_source_authorized=ReferenceVisible(current_->reference.context,
      current_->reference.page_identity,window_observation_,page_observation_);
  phase_=ReferencePhase::kCapturePending;
  status_="Draw around the reference";return true;
}

bool MarkController::BindPageIdentity(std::uint64_t generation,
                                      PageIdentity identity,
                                      std::optional<CapturedPageMetadata> captured_page) {
  Expire(clock_());
  if (!Current(generation) || current_->reference.page_identity || current_->reference.context.scope.policy_present) return false;
  if (!ValidPageIdentity(identity) ||
      !PageIdentityMatchesContext(identity,current_->reference.context)) {
    status_="Chrome page identity unavailable";
    return false;
  }
  if (captured_page) {
    // A forged/mismatched display value cannot change the page-identity gate.
    bool valid = captured_page->availability == SourceFieldAvailability::kCaptured &&
        captured_page->url && ValidSourceContextText(*captured_page->url,16384) &&
        captured_page->observed_utc_us && *captured_page->observed_utc_us > 0;
    if (valid) {
      static constexpr std::string_view domain="seethis/chrome-navigation/v1\0";
      Bytes input(domain.begin(),domain.end());
      input.insert(input.end(),captured_page->url->begin(),captured_page->url->end());
      valid = Sha256(input)==identity.navigation_digest;
    }
    if (!valid) {
      captured_page->url.reset();captured_page->observed_utc_us.reset();
      captured_page->availability=SourceFieldAvailability::kInvalidOrOverLimit;
    }
    current_->reference.captured_page=std::move(captured_page);
  }
  current_->reference.page_identity=std::move(identity);
  current_->ordinary_source_authorized=ReferenceVisible(current_->reference.context,
      current_->reference.page_identity,window_observation_,page_observation_);
  if(current_->released)AcceptReleased();
  return true;
}

void MarkController::Captured(std::uint64_t generation,CaptureResult result) {
  Expire(clock_());
  Transaction* transaction=Current(generation)?current_.get():nullptr;
  if(!transaction)for(auto& accepted:accepted_)
    if(accepted->generation==generation){transaction=accepted.get();break;}
  if(!transaction||transaction->capture||transaction->submitted)return;
  if(!result.error.empty()||!ValidPixels(result.pixels))
    transaction->capture_error=result.error.empty()?"capture_invalid":"capture_failed";
  else transaction->capture=std::move(result);
  if(transaction->released)SubmitIfReady(generation);
}

void MarkController::Release(std::uint64_t generation,
    std::vector<std::vector<DisplayPoint>> regions,Stamp time,
    std::int64_t key_up_monotonic_ms) {
  if(!Current(generation)||current_->released)return;
  current_->reference.schema=2;
  current_->reference.capture_api="ScreenCaptureKit.SCScreenshotManager.selected-display";
  current_->reference.provenance="historical;context-before-overlay;single-selected-display;pixels-at-async-completion;multi-region;no-semantic-object-id";
  current_->reference.path.clear();
  current_->reference.subpath_version = 0;
  current_->reference.regions.reserve(regions.size());
  for(auto& path:regions)current_->reference.regions.push_back({std::move(path),{}, {}, {}});
  ReleasePrepared(time,key_up_monotonic_ms);
}

void MarkController::Release(
    std::uint64_t generation,
    std::vector<InteractionSnapshot::LogicalRegion> regions, Stamp time,
    std::int64_t key_up_monotonic_ms) {
  if(!Current(generation)||current_->released)return;
  current_->reference.schema=2;
  current_->reference.capture_api="ScreenCaptureKit.SCScreenshotManager.selected-display";
  current_->reference.provenance="historical;context-before-overlay;single-selected-display;pixels-at-async-completion;multi-region;no-semantic-object-id";
  current_->reference.path.clear();
  current_->reference.subpath_version = 1;
  current_->reference.regions.reserve(regions.size());
  for(auto& region:regions)
    current_->reference.regions.push_back({{}, {}, {}, std::move(region)});
  ReleasePrepared(time,key_up_monotonic_ms);
}

void MarkController::Release(std::uint64_t generation,
    std::vector<DisplayPoint> path,Stamp time,
    std::int64_t key_up_monotonic_ms) {
  if(!Current(generation)||current_->released)return;
  if (current_->reference.page_identity || current_->reference.context.scope.policy_present ||
      current_->reference.context.bundle_id=="com.google.Chrome") {
    current_->reference.schema=2;
    current_->reference.capture_api="ScreenCaptureKit.SCScreenshotManager.selected-display";
    current_->reference.provenance="historical;context-before-overlay;single-selected-display;pixels-at-async-completion;multi-region;no-semantic-object-id";
    current_->reference.path.clear();
    current_->reference.subpath_version=0;
    current_->reference.regions={{std::move(path),{}, {}, {}}};
  } else {
    current_->reference.schema=1;current_->reference.path=std::move(path);
    current_->reference.regions.clear();
  }
  ReleasePrepared(time,key_up_monotonic_ms);
}

void MarkController::ReleasePrepared(Stamp time,
    std::int64_t key_up_monotonic_ms) {
  Expire(clock_());
  if(!current_)return;
  current_->released=true;
  current_->release_deadline_us=clock_().monotonic_us+30'000'000;
  current_->reference.circle_completed=time;
  current_->key_up_monotonic_ms=key_up_monotonic_ms;
  if(current_->reference.context.bundle_id=="com.google.Chrome"&&
     !current_->reference.context.scope.policy_present && !current_->reference.page_identity) {
    phase_=ReferencePhase::kReleasedPendingCapture;
    status_="Resolving Chrome page identity…";
    return;
  }
  AcceptReleased();
}

void MarkController::AcceptReleased() {
  if(!current_||!current_->released)return;
  if(!current_->append_root.empty()) {
    const auto generation=current_->generation;
    current_->append_deadline_us=clock_().monotonic_us+30'000'000;
    accepted_.push_back(std::move(current_));
    phase_=ReferencePhase::kReleasedPendingCapture;status_="Adding screenshot…";
    SubmitIfReady(generation);return;
  }
  auto job=store_.AcceptPending(current_->reference,current_->key_down_monotonic_ms,
                                current_->key_up_monotonic_ms);
  ++copy_generation_;current_->copy_generation=copy_generation_;
  last_copy_id_=current_->reference.id;
  if(job.state==ReferenceJobState::kPending&&pending_clipboard_) {
    try{current_->pasteboard_change_count=pending_clipboard_(current_->reference.id);}
    catch(...){current_->pasteboard_change_count.reset();}
    copy_result_=current_->pasteboard_change_count?CopyResult::kCopied:CopyResult::kFailed;
  }
  const auto accepted_generation=current_->generation;
  accepted_.push_back(std::move(current_));
  phase_=job.state!=ReferenceJobState::kPending?ReferencePhase::kAborted:
         ReferencePhase::kReleasedPendingCapture;
  status_=job.state!=ReferenceJobState::kPending?
      "Reference failed: "+(job.code.empty()?"identity_exists":job.code):
      "Reference loading…";
  if(job.state!=ReferenceJobState::kPending) {
    accepted_.pop_back();
    copy_result_=CopyResult::kFailed;
    return;
  }
  SubmitIfReady(accepted_generation);
}

void MarkController::SubmitIfReady(std::uint64_t generation) {
  auto found=std::find_if(accepted_.begin(),accepted_.end(),[&](const auto& item){
    return item->generation==generation;});
  if(found==accepted_.end()||(*found)->submitted)return;
  auto& transaction=**found;
  auto weak=std::weak_ptr<int>(lifetime_);
  auto dispatcher=dispatcher_;
  auto completion=[this,weak,dispatcher=std::move(dispatcher)](
      ReferenceJobSnapshot job) mutable {
    dispatcher([this,weak,job=std::move(job)]() mutable {
      if(!weak.lock())return;Completed(std::move(job));
    });
  };
  if(!transaction.append_root.empty()) {
    if(!transaction.capture&&!transaction.capture_error.empty()) {
      phase_=ReferencePhase::kAborted;status_="Append failed: "+transaction.capture_error;
      if(append_completion_)append_completion_({AppendStatus::kInvalid,transaction.append_root,transaction.reference.id,0},generation);
      accepted_.erase(found);return;
    }
    if(!transaction.capture)return;
    transaction.submitted=true;
    const auto root=transaction.append_root,operation=transaction.reference.id;
    const auto epoch=transaction.input_epoch;const bool healthy=transaction.input_healthy;
    const auto expected=transaction.expected_revision;
    const auto continuous=transaction.context_continuous;
    const auto single=transaction.single_checkpoint;
    const auto presentation_generation=transaction.presentation_generation;
    const bool ordinary_source_authorized=transaction.ordinary_source_authorized;
    const auto coordinate_loss=transaction.coordinate_loss;
    Reference capture_source;capture_source.context=transaction.reference.context;capture_source.page_identity=transaction.reference.page_identity;
    auto dispatch=dispatcher_;
    store_.AppendReferenceAsync(root,expected,operation,
        transaction.reference,std::move(*transaction.capture),
        [this,weak,dispatch,root,operation,epoch,healthy,generation,continuous,single,presentation_generation,ordinary_source_authorized,coordinate_loss,capture_source](AppendResult result) {
      dispatch([this,weak,root,operation,epoch,healthy,generation,result,continuous,single,presentation_generation,ordinary_source_authorized,coordinate_loss,capture_source] {
        if(!weak.lock())return;
        const bool success=result.status==AppendStatus::kCommitted||result.status==AppendStatus::kIdempotent;
        if(success&&coordinate_loss->load()) {
          const auto collection=store_.Collection(root);
          if(collection.status==MarkNotesStatus::kReady)for(const auto& shot:collection.shots)if(shot.id==operation&&SameSavedSource(capture_source,*shot.metadata))
            ExcludeSavedShot(shot,static_cast<SavedCoordinateLoss>(coordinate_loss->load()-1));
        }
        PreparePresentationObservation();
        if(success&&healthy&&input_healthy_&&input_epoch_==epoch&&continuous->load()&&
           presentation_permitted_&&!presentation_prefix_&&presentation_generation==presentation_generation_) {
          const auto collection=store_.Collection(root);
          const auto root_witness=[&](const auto& entries) {
            return std::any_of(entries.begin(),entries.end(),[&](const auto& entry) {
              return entry.epoch==epoch&&entry.shot.metadata->id==root&&PresentedReferenceVisible(*entry.shot.metadata);
            });
          };
          // This completes only an already externally authorized append. The
          // private channel cannot authorize Begin/Bind, a history root, or a
          // new capture made while our panels own foreground.
          const bool bound_completion=ordinary_source_authorized&&own_input_focus_&&own_ui_source_&&
              own_ui_source_->epoch==epoch&&(root_witness(admitted_)||root_witness(presented_singles_))&&
              ReferenceVisible(own_ui_source_->context,own_ui_source_->page,own_ui_window_,own_ui_page_);
          if(collection.status==MarkNotesStatus::kReady&&!collection.shots.empty()&&collection.shots.back().id==operation&&
             (ReferenceVisible(collection.shots.back().metadata->context,
                collection.shots.back().metadata->page_identity,window_observation_,page_observation_)||
              (bound_completion&&ReferenceVisible(collection.shots.back().metadata->context,
                collection.shots.back().metadata->page_identity,own_ui_window_,own_ui_page_)))) {
            if(single&&admitted_.size()<500*64&&std::none_of(admitted_.begin(),admitted_.end(),
                [&](const auto& entry){return entry.shot.id==single->id&&entry.shot.metadata->id==root;}))
              admitted_.push_back({*single,epoch});
            if(admitted_.size()<500*64&&std::none_of(admitted_.begin(),admitted_.end(),
                [&](const auto& entry){return entry.shot.id==operation&&entry.shot.metadata->id==root;}))
              admitted_.push_back({collection.shots.back(),epoch});
          }
        }
        std::erase_if(accepted_,[&](const auto& item){return item->generation==generation;});
        if(generation==generation_) {
          phase_=success?ReferencePhase::kCommitted:ReferencePhase::kAborted;
          status_=success?"Screenshot added":result.status==AppendStatus::kCommitUncertain?
            "Append publication uncertain; reload by operation identity":"Append failed; saved reference retained";
        }
        if(append_completion_)append_completion_(result,generation);
      });
    },transaction.append_deadline_us);
    return;
  }
  if(!transaction.capture_error.empty()) {
    transaction.submitted=true;
    store_.Fail(transaction.reference.id,transaction.capture_error,
                std::move(completion));return;
  }
  if(!transaction.capture)return;
  transaction.submitted=true;
  store_.FinalizeAsync(transaction.reference.id,std::move(*transaction.capture),
                       std::move(completion));
}

void MarkController::Completed(ReferenceJobSnapshot job) {
  auto transaction=std::find_if(accepted_.begin(),accepted_.end(),[&](const auto& item){
    return item->reference.id==job.id;});
  if(transaction!=accepted_.end()&&(*transaction)->status_action_only)return; // Duplicate completion has no publication authority.
  const bool owns_ui=transaction!=accepted_.end()&&
      (*transaction)->copy_generation==copy_generation_;
  if(transaction!=accepted_.end()&&job.state==ReferenceJobState::kReady&&(*transaction)->coordinate_loss->load()) {
    const auto collection=store_.Collection(job.id);
    if(collection.status==MarkNotesStatus::kReady)for(const auto& shot:collection.shots)if(shot.id==job.id&&SameSavedSource((*transaction)->reference,*shot.metadata))
      ExcludeSavedShot(shot,static_cast<SavedCoordinateLoss>((*transaction)->coordinate_loss->load()-1));
  }
  if(pending_clipboard_) {
    if(owns_ui&&job.state==ReferenceJobState::kReady) {
      // The URL publication happened at release. Completion only reports the
      // reference lifecycle; it cannot read or establish clipboard ownership.
      if(!(*transaction)->pasteboard_change_count)
        copy_result_=CopyResult::kFailed;
      else if(ready_clipboard_)try {
        // This callback is an optional ownership guard supplied by the caller;
        // a successful result does not turn completion into a publication.
        if(!ready_clipboard_(job.ready,
                             *(*transaction)->pasteboard_change_count))
          copy_result_=CopyResult::kPersistedClipboardFailed;
      } catch(...) {copy_result_=CopyResult::kPersistedClipboardFailed;}
      status_="Reference ready; click its mark to copy the URL";
      if(!current_)phase_=ReferencePhase::kCommitted;
    } else if(owns_ui) {
      copy_result_=CopyResult::kFailed;status_="Reference failed: "+job.code;
      if(!current_)phase_=ReferencePhase::kAborted;
    }
  } else if(job.state==ReferenceJobState::kReady&&job.ready) {
    bool copied=false;
    if(clipboard_)try{copied=clipboard_(*job.ready);}catch(...){copied=false;}
    copy_result_=copied?CopyResult::kCopied:CopyResult::kPersistedClipboardFailed;
    status_=copied?"Reference ready and copied":
        "Reference ready; click its mark to retry";
    if(!current_)phase_=ReferencePhase::kCommitted;
  } else if(!pending_clipboard_) {
    copy_result_=CopyResult::kFailed;status_="Reference failed: "+job.code;
    if(!current_)phase_=ReferencePhase::kAborted;
  }
  if(transaction!=accepted_.end()&&job.state==ReferenceJobState::kReady&&job.metadata) {
    const auto& captured=**transaction;
    if(captured.input_healthy&&input_healthy_&&captured.input_epoch==input_epoch_&&
       captured.presentation_generation==presentation_generation_&&captured.context_continuous->load()&&
       presentation_permitted_&&!presentation_prefix_&&
       ReferenceVisible(job.metadata->context,job.metadata->page_identity,window_observation_,page_observation_)) {
      std::erase_if(fresh_singles_,[&](const auto& entry){return entry.shot.metadata->id==job.id;});
      if(fresh_singles_.size()<500)fresh_singles_.push_back({{job.id,0,job.metadata},input_epoch_});
    }
  }
  if(transaction!=accepted_.end()) {
    if(job.state==ReferenceJobState::kFailed&&pending_clipboard_&&(*transaction)->append_root.empty()) {
      // Keep only this controller's accepted root-source custody for explicit
      // failure status actions. No pixels, submission or completion is retained.
      (*transaction)->status_action_only=true;(*transaction)->submitted=true;
      (*transaction)->capture.reset();(*transaction)->single_checkpoint.reset();
      (*transaction)->pasteboard_change_count.reset();(*transaction)->copy_generation=0;
      Bytes{}.swap((*transaction)->reference.source.bytes);Bytes{}.swap((*transaction)->reference.crop.bytes);
      (*transaction)->start_window={};(*transaction)->start_page={};
      // Retain the newest bounded metadata witnesses in acceptance order.
      while(std::count_if(accepted_.begin(),accepted_.end(),[](const auto& item) {return item->status_action_only;})>500) {
        const auto oldest=std::find_if(accepted_.begin(),accepted_.end(),[](const auto& item) {return item->status_action_only;});
        accepted_.erase(oldest);
      }
    } else accepted_.erase(transaction);
  }
}

void MarkController::Abort(std::string reason) {
  if(!current_) {
    const auto pending=std::find_if(accepted_.begin(),accepted_.end(),[&](const auto& transaction) {
      return transaction->generation==generation_&&!transaction->append_root.empty();
    });
    if(pending==accepted_.end())return;
    store_.CancelAppend((*pending)->reference.id);accepted_.erase(pending);
  }
  ++generation_;current_.reset();phase_=ReferencePhase::kAborted;
  copy_result_=CopyResult::kFailed;status_="Reference cancelled: "+std::move(reason);
}

void MarkController::Expire(Stamp now) {
  PrunePresentations();
  const auto capture_wait_expired=[&](const auto& transaction) {
    return !transaction->submitted&&!transaction->capture&&transaction->capture_error.empty()&&
        now.monotonic_us>transaction->capture_deadline_us;
  };
  if(current_&&capture_wait_expired(current_))Abort("screenshot service timed out");
  else if(current_&&current_->released&&now.monotonic_us>current_->release_deadline_us)
    Abort("released capture/page wait timed out");
  std::vector<std::pair<AppendResult,std::uint64_t>> expired;
  std::vector<std::uint64_t> failed_captures;
  std::erase_if(accepted_,[&](const auto& transaction) {
    if(transaction->status_action_only)return false;
    if(transaction->append_root.empty()) {
      if(capture_wait_expired(transaction))failed_captures.push_back(transaction->generation);
      return false;
    }
    if(!capture_wait_expired(transaction)&&now.monotonic_us<=transaction->append_deadline_us)return false;
    store_.CancelAppend(transaction->reference.id);
    expired.push_back({{AppendStatus::kUnavailable,transaction->append_root,transaction->reference.id,0},transaction->generation});
    if(transaction->generation==generation_) {
      ++generation_;phase_=ReferencePhase::kAborted;status_="Append timed out; saved reference retained";
    }
    return true;
  });
  // Store callbacks may synchronously remove accepted entries; never invoke
  // them while iterating/erasing that vector.
  for(const auto generation:failed_captures) {
    const auto found=std::find_if(accepted_.begin(),accepted_.end(),[&](const auto& item){return item->generation==generation;});
    if(found==accepted_.end())continue;
    (*found)->capture_error="capture_timeout";SubmitIfReady(generation);
  }
  for(const auto& [result,generation]:expired)if(append_completion_)append_completion_(result,generation);
}

bool MarkController::AllowsDrawing() const {
  return current_&&current_->reference.context.pid>0&&!current_->released;
}

void MarkController::CheckPendingWindowContinuity(const WindowObservation& observation) {
  if(own_input_focus_&&observation.availability==WindowAvailability::kUnavailable)return; // Own UI has no external source window.
  const auto check=[&](const std::unique_ptr<Transaction>& transaction) {
    if(!transaction)return;
    const auto& captured=transaction->reference.context;
    bool continuous=false;
    if(captured.pid>0) {
      continuous=WindowObservationMatchesContext(captured,observation) &&
          (captured.scope.kind==CaptureScopeKind::kApplication||(captured.scope.kind==CaptureScopeKind::kWindow&&!captured.scope.title_required)||captured.bundle_id=="com.google.Chrome"||TitleContextVisible(captured,observation));
    } else {
      const auto& first=transaction->start_window;
      Context baseline;baseline.pid=baseline.window_pid=first.pid;baseline.bundle_id=first.bundle_id;
      baseline.window_id=first.window_id;baseline.window=first.bounds;
      baseline.native_window=first.native_window;baseline.observed=first.observed;
      continuous=first.availability==WindowAvailability::kReady &&
          WindowObservationMatchesContext(baseline,observation) &&
          (first.bundle_id=="com.google.Chrome"||
           (first.title.version==observation.title.version && first.title.availability==observation.title.availability &&
            first.window_title==observation.window_title));
    }
    if(!continuous)transaction->context_continuous->store(false);
    else if(ReferenceVisible(captured,transaction->reference.page_identity,observation,page_observation_))
      transaction->ordinary_source_authorized=true;
  };
  check(current_);for(const auto& transaction:accepted_)check(transaction);
}
void MarkController::CheckPendingPageContinuity(const PageObservation& observation) {
  if(own_input_focus_&&observation.availability==PageAvailability::kUnavailable)return;
  const auto check=[&](const std::unique_ptr<Transaction>& transaction) {
    if(!transaction)return;
    const auto& reference=transaction->reference;
    const bool chrome=reference.context.bundle_id=="com.google.Chrome" ||
        (reference.context.pid==0 && transaction->start_window.bundle_id=="com.google.Chrome");
    if(!chrome || (reference.context.scope.policy_present && reference.context.scope.kind!=CaptureScopeKind::kPage))return;
    const auto& required=reference.page_identity?reference.page_identity:transaction->start_page.identity;
    if(observation.availability!=PageAvailability::kReady||!required||
       !PageIdentityVisible(required,observation))transaction->context_continuous->store(false);
    else if(ReferenceVisible(reference.context,reference.page_identity,window_observation_,observation))
      transaction->ordinary_source_authorized=true;
  };
  check(current_);for(const auto& transaction:accepted_)check(transaction);
}
bool MarkController::LiveActionVisible(const ReferenceJobSnapshot& job) const {
  if(job.state==ReferenceJobState::kPending||job.state==ReferenceJobState::kFailed) {
    // A status URL is an explicit live action, never a saved-shot projection.
    // Recheck current source evidence even if capture continuity was lost;
    // completion still retains its own irreversible continuity/ownership gates.
    if(!job.metadata||job.metadata->id!=job.id||job.code=="invalid"||!input_healthy_||
       !presentation_permitted_||presentation_prefix_||
       !KnownTopology(job.context.displays)||
       (display_observation_&&(!KnownTopology(*display_observation_)||
        !DisplayTopologyMatches(job.context.displays,*display_observation_)))||
       !ReferenceVisible(job.metadata->context,job.metadata->page_identity,
                         window_observation_,page_observation_))return false;
    {
      const auto captured=std::find_if(accepted_.begin(),accepted_.end(),[&](const auto& transaction) {
        return transaction->append_root.empty()&&transaction->reference.id==job.id;
      });
      // Only this controller's accepted source owns Pending/Failed actions. A
      // fresh observation cannot manufacture health or undo native replacement.
      if(captured==accepted_.end()||!(*captured)->input_healthy||
         (*captured)->input_epoch!=input_epoch_||
         (*captured)->presentation_generation!=presentation_generation_||(*captured)->coordinate_loss->load()||
         !SameSavedSource((*captured)->reference,*job.metadata))return false;
    }
    return true;
  }
  if(job.state!=ReferenceJobState::kReady)return false;
  if(job.state==ReferenceJobState::kReady) {
    const auto collection=store_.Collection(job.id);
    if(collection.multi())return collection.status==MarkNotesStatus::kReady&&LiveShot(job.id).has_value();
    if(collection.shots.empty())return false; // malformed collection never becomes legacy
    // A legacy single-shot copy/delete keeps its original context-only gate,
    // including when its optional notes sidecar is unreadable.
  }
  return job.metadata?PresentedReferenceVisible(*job.metadata):VisibleJob(job,window_observation_,page_observation_);
}

void MarkController::SetWindowObservation(WindowObservation observation) {
  if(observation.generation<=window_observation_.generation)return;
  PreparePresentationObservation();
  if(own_ui_source_&&observation.availability==WindowAvailability::kReady&&
     (!WindowObservationMatchesContext(own_ui_source_->context,observation)||
      (own_ui_source_->context.scope.kind!=CaptureScopeKind::kApplication&&
       (!own_ui_source_->context.scope.policy_present||own_ui_source_->context.scope.title_required)&&own_ui_source_->context.bundle_id!="com.google.Chrome"&&
       !TitleContextVisible(own_ui_source_->context,observation))))RetireLiveShot();
  ObserveSavedWindowGeometry(observation);
  CheckPendingWindowContinuity(observation);
  window_observation_=std::move(observation);
  PrunePresentations();
}

void MarkController::SetPageObservation(PageObservation observation) {
  if (observation.generation <= page_observation_.generation) return;
  PreparePresentationObservation();
  if (observation.availability == PageAvailability::kReady &&
      (!observation.identity || !ValidPageIdentity(*observation.identity))) {
    observation.availability=PageAvailability::kUnavailable;
    observation.identity.reset();
    if(own_ui_source_&&own_ui_source_->page)RetireLiveShot();
  }
  if(own_ui_source_&&own_ui_source_->page&&
     ((observation.availability==PageAvailability::kReady&&!PageIdentityVisible(own_ui_source_->page,observation))||
      (observation.availability!=PageAvailability::kReady&&observation.availability!=PageAvailability::kUnavailable)))RetireLiveShot();
  CheckPendingPageContinuity(observation);
  page_observation_=std::move(observation);
  presentation_state_.reset();
  PrunePresentations();
}

void MarkController::PrepareCurrentPresentation() const {
  const auto state=store_.ReadPresentation();
  if(state==presentation_state_&&presentation_window_generation_==window_observation_.generation&&
     presentation_own_window_generation_==own_ui_window_.generation&&presentation_own_generation_==own_ui_generation_)return;
  presentation_state_=state;presentation_window_generation_=window_observation_.generation;
  presentation_own_window_generation_=own_ui_window_.generation;presentation_own_generation_=own_ui_generation_;
  presentation_candidates_.clear();presentation_shots_.clear();
  // This history walk happens only on a store publication or source observation,
  // never once per pointer sample. Current evidence is still judged at use time.
  const auto currentSource=[&](const Reference& ref) {
    return ReferenceVisible(ref.context,ref.page_identity,window_observation_,page_observation_)||
        (own_ui_source_&&ReferenceVisible(ref.context,ref.page_identity,own_ui_window_,own_ui_page_));
  };
  std::vector<std::shared_ptr<const ReferencePresentation>> sourceEntries;
  std::set<std::string> seen;
  const auto append=[&](const auto& index,const std::string& key) {
    const auto found=index.find(key);if(found==index.end())return;
    for(const auto& entry:found->second)if(seen.insert(entry->job.id).second)sourceEntries.push_back(entry);
  };
  const auto select=[&](const WindowObservation& observation) {
    if(observation.window_id)append(state->by_window,WindowSourceKey(observation.pid,observation.window_id));
    if(observation.application_available&&display_observation_)
      for(const auto& display:*display_observation_)append(state->by_application,
          ApplicationSourceKey(observation.pid,observation.application_start_identity_us,display.id));
    if(observation.application_available&&display_observation_&&observation.kernel_application_start_identity_us>0)
      for(const auto& display:*display_observation_)append(state->by_application,
          ApplicationSourceKey(observation.pid,observation.kernel_application_start_identity_us,display.id,ProcessStartSource::kKernelBSDStart));
  };
  select(window_observation_);if(own_ui_source_)select(own_ui_window_);
  std::sort(sourceEntries.begin(),sourceEntries.end(),[](const auto& a,const auto& b) {
    return a->job.accepted_utc_us==b->job.accepted_utc_us?a->job.id<b->job.id:a->job.accepted_utc_us<b->job.accepted_utc_us;
  });
  for(const auto& entry:sourceEntries) {
    bool candidate=false;
    if(entry->job.state==ReferenceJobState::kReady) {
      for(const auto& shot:entry->collection.shots)if(shot.metadata&&currentSource(*shot.metadata)) {
        presentation_shots_[entry->job.id].push_back(shot);candidate=true;
      }
    } else if(entry->job.metadata)candidate=currentSource(*entry->job.metadata);
    if(candidate)presentation_candidates_.push_back(entry);
  }
}
std::vector<ReferenceJobSnapshot> MarkController::presentation_jobs() const {
  PrepareCurrentPresentation();std::vector<ReferenceJobSnapshot> result;
  for(const auto& entry:presentation_candidates_) {
    if(!store_.ReadPresentation(entry->job.id))continue;
    if(entry->job.state==ReferenceJobState::kReady) {
      const auto& shots=presentation_shots_.at(entry->job.id);
      if(std::none_of(shots.begin(),shots.end(),
          [&](const auto& shot){return shot.metadata&&PresentedReferenceVisible(*shot.metadata);}))continue;
    } else if(!VisibleJob(entry->job,window_observation_,page_observation_))continue;
    result.push_back(entry->job);
  }
  return result;
}
std::vector<ReferenceMark> MarkController::marks() const {
  PrepareCurrentPresentation();std::vector<ReferenceMark> result;
  for(const auto& entry:presentation_candidates_)if(entry->job.state==ReferenceJobState::kReady)
    for(const auto& shot:presentation_shots_.at(entry->job.id))if(shot.metadata&&PresentedReferenceVisible(*shot.metadata))
      result.push_back(LightweightMark(*shot.metadata));
  return result;
}

MarkNotesSnapshot MarkController::LiveMarkSnapshots(std::string_view root) const {
  MarkNotesSnapshot result;
  const auto entry=store_.ReadPresentation(root);if(!entry)return result;
  result.status=entry->notes.status;result.revision=entry->notes.revision;
  for(const auto& shot:LiveShots(root)) {
    const auto notes=entry->shot_notes.find(shot.id);
    if(notes!=entry->shot_notes.end())result.marks.insert(result.marks.end(),notes->second.marks.begin(),notes->second.marks.end());
  }
  return result;
}

std::vector<ReferenceJobSnapshot> MarkController::jobs() const {
  auto result=presentation_jobs();
  std::erase_if(result,[](const auto& job){return job.state==ReferenceJobState::kReady;});
  return result;
}

std::vector<ReferenceJobSnapshot> MarkController::inspector_jobs() const {
  auto result=store_.MetadataJobs();
  result.erase(std::remove_if(result.begin(),result.end(),[](const auto& job) {
    return job.state==ReferenceJobState::kDeleted;
  }),result.end());
  return result;
}

bool MarkController::NeedsChromePageObservation() const {
  if(current_&&current_->reference.context.bundle_id=="com.google.Chrome")
    return true;
  return store_.ReadPresentation()->has_chrome_context;
}

bool MarkController::AwaitingPageIdentity(std::uint64_t generation) const {
  return Current(generation)&&!current_->reference.context.scope.policy_present&&current_->released&&
      current_->reference.context.bundle_id=="com.google.Chrome"&&
      !current_->reference.page_identity;
}

bool MarkController::Recopy(std::size_t index) {
  const auto retained=marks();if(index>=retained.size())return false;
  return RecopyJob(retained[index].value.id);
}

bool MarkController::RecopyJob(std::string_view id) {
  auto job=store_.LookupMetadata(id);if(!job)return false;++copy_generation_;
  if(!LiveActionVisible(*job)) {
    copy_result_=CopyResult::kFailed;
    status_="Reference hidden for current window";
    return false;
  }
  last_copy_id_=std::string(id);
  if(job->state==ReferenceJobState::kExpired) {
    copy_result_=CopyResult::kFailed;status_="Reference expired";return false;
  }
  if(job->state==ReferenceJobState::kDeleted) {
    copy_result_=CopyResult::kFailed;status_="Reference deleted";return false;
  }
  if(pending_clipboard_) {
    auto receipt=pending_clipboard_(id);for(auto& accepted:accepted_)
      if(accepted->reference.id==id&&!accepted->status_action_only){accepted->copy_generation=copy_generation_;
        accepted->pasteboard_change_count=receipt;}
    copy_result_=receipt?CopyResult::kCopied:CopyResult::kFailed;
    status_=receipt?"Reference URL copied":"Reference URL could not be copied";
    return receipt.has_value();
  }
  if(job->state!=ReferenceJobState::kReady)return false;
  auto ready=store_.Lookup(id);if(!ready||!ready->ready)return false;
  bool success=false;try{success=clipboard_&&clipboard_(*ready->ready);}
  catch(...){success=false;}
  copy_result_=success?CopyResult::kCopied:CopyResult::kPersistedClipboardFailed;
  status_=success?"Reference URL copied":
      "Reference saved; clipboard failed. Click its mark to retry.";
  return success;
}

bool MarkController::CopyHistoryJob(std::string_view id) {
  const auto metadata=store_.LookupMetadata(id);
  if(!metadata || metadata->state!=ReferenceJobState::kReady) {
    copy_result_=CopyResult::kFailed;
    status_="Reference is not ready for history copy";
    return false;
  }
  const auto ready=store_.Lookup(id);
  if(!ready || ready->state!=ReferenceJobState::kReady || !ready->ready) {
    copy_result_=CopyResult::kFailed;
    status_="Persisted reference is unavailable for history copy";
    return false;
  }
  ++copy_generation_;
  last_copy_id_=std::string(id);
  bool copied=false;
  if(pending_clipboard_) {
    try {copied=pending_clipboard_(id).has_value();}
    catch(...) {copied=false;}
  } else if(clipboard_) {
    try {copied=clipboard_(*ready->ready);}
    catch(...) {copied=false;}
  }
  copy_result_=copied?CopyResult::kCopied:CopyResult::kPersistedClipboardFailed;
  status_=copied?"Reference URL copied":"Reference URL could not be copied";
  return copied;
}

DeleteResult MarkController::DeleteJob(std::string_view id) {
  ++copy_generation_;
  auto metadata=store_.LookupMetadata(id);
  if(!metadata ||
     (metadata->state!=ReferenceJobState::kDeleted&&
      !LiveActionVisible(*metadata))) {
    status_="Reference hidden for current window";
    return DeleteResult::kNotFound;
  }
  return DeleteStoredJob(id);
}

DeleteResult MarkController::DeleteHistoryJob(std::string_view id) {
  ++copy_generation_;
  if(!store_.LookupMetadata(id)) {
    status_="Reference not found";
    return DeleteResult::kNotFound;
  }
  return DeleteStoredJob(id);
}

DeleteResult MarkController::DeleteStoredJob(std::string_view id) {
  std::erase_if(admitted_,[&](const auto& entry){return entry.shot.metadata->id==id;});
  std::erase_if(presented_singles_,[&](const auto& entry){return entry.shot.metadata->id==id;});
  std::erase_if(fresh_singles_,[&](const auto& entry){return entry.shot.metadata->id==id;});
  const auto result=store_.Delete(id);
  accepted_.erase(std::remove_if(accepted_.begin(),accepted_.end(),
    [&](const auto& transaction){return transaction->reference.id==id;}),
    accepted_.end());
  if(result==DeleteResult::kDeleted)status_="Reference deleted from this Mac";
  else if(result==DeleteResult::kAlreadyDeleted)status_="Reference already deleted";
  else if(result==DeleteResult::kCleanupFailed)
    status_="Delete incomplete: local reference cleanup failed";
  else status_="Reference not found";
  return result;
}

std::optional<std::size_t> MarkController::Hit(DisplayPoint point,double radius,
    const std::vector<FrozenDisplay>& displays) const {
  if(!point.IsValid()||point.unit!=CoordinateUnit::kLogicalPoints||
     !std::isfinite(radius)||radius<0)return {};
  const auto retained=marks();
  for(std::size_t index=retained.size();index>0;--index)
    if(HitRegions(retained[index-1].value,point,radius,displays))return index-1;
  return {};
}

std::optional<std::string> MarkController::HitJob(
    DisplayPoint point,double radius,
    const std::vector<FrozenDisplay>& displays) const {
  if(!point.IsValid()||point.unit!=CoordinateUnit::kLogicalPoints||
     !std::isfinite(radius)||radius<0)return {};
  auto snapshots=jobs();
  for(auto job=snapshots.rbegin();job!=snapshots.rend();++job)
    {
      Reference value;value.context=job->context;value.path=job->path;
      value.regions=job->regions;
      value.page_identity=job->page_identity;
      if(HitRegions(value,point,radius,displays))return job->id;
    }
  return {};
}

std::optional<std::string> MarkController::HitIdentity(
    DisplayPoint point,double radius,
    const std::vector<FrozenDisplay>& displays) const {
  if(!PresentationHitsAllowed()||!point.IsValid()||point.unit!=CoordinateUnit::kLogicalPoints||
     !std::isfinite(radius)||radius<0)return {};
  auto snapshots=presentation_jobs();
  for(auto job=snapshots.rbegin();job!=snapshots.rend();++job) {
    if(job->state==ReferenceJobState::kExpired||
       job->state==ReferenceJobState::kDeleted)continue;
    if(job->state==ReferenceJobState::kReady) {
      const auto entry=store_.ReadPresentation(job->id);if(!entry)continue;
      const auto& collection=entry->collection;
      if(collection.status!=MarkNotesStatus::kReady)continue;
      if(collection.multi()) {
        const auto live=LiveShots(job->id);
        for(auto shot=live.rbegin();shot!=live.rend();++shot)
          if(HitRegions(*shot->metadata,point,radius,displays))return job->id;
        continue;
      }
    }
    if(!(job->metadata?PresentedReferenceVisible(*job->metadata):VisibleJob(*job,window_observation_,page_observation_)))continue;
    Reference value;value.context=job->context;value.path=job->path;
    value.regions=job->regions;
    value.page_identity=job->page_identity;
    if(job->metadata)value=*job->metadata;
    else if(job->ready)value=job->ready->value;
    if(HitRegions(value,point,radius,displays))return job->id;
  }
  return {};
}
}  // namespace seethis::core
