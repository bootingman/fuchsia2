// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/agents/module_suggester.h"

#include <rapidjson/document.h>

#include "apps/maxwell/services/context/client.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_agent_client.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "lib/mtl/tasks/message_loop.h"

constexpr char maxwell::agents::ModuleSuggesterAgent::kModuleSuggestionId[];

// TODO(afergan): Once we can populate modular_state with actual data, we
// can decide which module to launch, and actually launch it. For now, we just
// create a proposal with the mail headline.
const std::string kMailHeadline = "Open Mail";

namespace {

class ModuleSuggesterAgentApp : public maxwell::agents::ModuleSuggesterAgent,
                                public maxwell::context::SubscriberLink {
 public:
  ModuleSuggesterAgentApp()
      : app_context_(modular::ApplicationContext::CreateFromStartupInfo()),
        maxwell_context_(app_context_->ConnectToEnvironmentService<
                         maxwell::context::SuggestionAgentClient>()),
        in_(this),
        out_(app_context_->ConnectToEnvironmentService<
             maxwell::suggestion::SuggestionAgentClient>()) {
    fidl::InterfaceHandle<maxwell::context::SubscriberLink> in_handle;
    in_.Bind(&in_handle);
    maxwell_context_->Subscribe("/modular_status", "json:int",
                                std::move(in_handle));
  }

  void OnUpdate(maxwell::context::UpdatePtr update) override {
    FTL_LOG(INFO) << "OnUpdate from " << update->source << ": "
                  << update->json_value;

    rapidjson::Document d;
    d.Parse(update->json_value.data());

    if (d.IsInt()) {
      const int modular_state = d.GetInt();

      std::string module_suggestion;

      if (modular_state == 0) {
        out_->Remove(kModuleSuggestionId);
      } else {
          auto p = maxwell::suggestion::Proposal::New();
          p->id = kModuleSuggestionId;
          p->on_selected = fidl::Array<maxwell::suggestion::ActionPtr>::New(0);
          auto d = maxwell::suggestion::Display::New();

          d->headline = kMailHeadline;
          d->subheadline = "";
          d->details = "";
          d->color = 0x00aaaa00; // argb yellow
          d->icon_urls = fidl::Array<fidl::String>::New(1);
          d->icon_urls[0] = "";
          d->image_url = "";
          d->image_type = maxwell::suggestion::SuggestionImageType::PERSON;

          p->display = std::move(d);

          out_->Propose(std::move(p));
        }
    }
  }

 private:
  std::unique_ptr<modular::ApplicationContext> app_context_;

  maxwell::context::SuggestionAgentClientPtr maxwell_context_;
  fidl::Binding<maxwell::context::SubscriberLink> in_;
  maxwell::suggestion::SuggestionAgentClientPtr out_;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  ModuleSuggesterAgentApp app;
  loop.Run();
  return 0;
}
