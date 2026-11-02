#include "plugin/plugin_processor.h"

#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/utility/memoryibstream.h"
#include "choc/text/choc_JSON.h"

#include <array>
#include <cmath>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../support/webview_test_lifetime.h"

namespace effetune::vst::plugin {
class PluginProcessorTestAccess {
public:
  static choc::value::Value playbackAt(EffeTuneProcessor &processor, const std::int64_t now) {
    auto result = choc::value::createObject({});
    processor.appendPlaybackSnapshot(result, now);
    return choc::value::Value(result["playback"]);
  }
};
}

namespace {
using namespace Steinberg;
using namespace Steinberg::Vst;
using effetune::vst::plugin::EffeTuneProcessor;

class IncompleteMessage final : public U::Implements<U::Directly<IMessage>> {
public:
  explicit IncompleteMessage(const char *id) : id_(id) {}
  FIDString PLUGIN_API getMessageID() override { return id_; }
  void PLUGIN_API setMessageID(FIDString id) override { id_ = id; }
  IAttributeList *PLUGIN_API getAttributes() override { return nullptr; }

private:
  const char *id_;
};

void expect(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

IPtr<EffeTuneProcessor> makeProcessor() {
  auto processor = owned(new EffeTuneProcessor);
  expect(processor->initialize(nullptr) == kResultOk, "initialize metadata processor");
  return processor;
}

IPtr<HostMessage> makeMessage(const char *id = "EffeTune.NowPlaying") {
  auto message = owned(new HostMessage);
  message->setMessageID(id);
  return message;
}

choc::value::Value readSnapshot(EffeTuneProcessor &processor,
                               const std::string_view type = "host/getInfo",
                               const std::int64_t revision = 0) {
  return choc::json::parse(processor.handleUiMessage(
      "{\"type\":\"" + std::string(type) +
      "\",\"payload\":{\"nowPlayingRevision\":" + std::to_string(revision) + "}}"));
}

std::int64_t revisionOf(const choc::value::Value &response) {
  expect(response["ok"].getWithDefault<bool>(false), "metadata bridge request succeeds");
  return response["nowPlayingRevision"].getWithDefault<std::int64_t>(0);
}

std::string snapshotJson(EffeTuneProcessor &processor) {
  return choc::json::toString(readSnapshot(processor)["nowPlaying"]);
}

void expectRejected(EffeTuneProcessor &processor, IMessage *message,
                    const char *reason) {
  const auto before = readSnapshot(processor);
  expect(processor.notify(message) == kInvalidArgument, reason);
  const auto after = readSnapshot(processor);
  expect(revisionOf(after) == revisionOf(before), "rejection preserves revision");
  expect(choc::json::toString(after["nowPlaying"]) ==
             choc::json::toString(before["nowPlaying"]),
         "rejection preserves the complete previous snapshot");
}

void testInterfacesAndOrdinaryHost() {
  auto processor = makeProcessor();
  FUnknownPtr<IComponent> component(static_cast<IComponent *>(processor.get()));
  FUnknownPtr<IEditController> controller(static_cast<IComponent *>(processor.get()));
  expect(component != nullptr && controller != nullptr,
         "single component exposes both processor and controller");
  FUnknownPtr<IConnectionPoint> fromComponent(component.getInterface());
  FUnknownPtr<IConnectionPoint> fromController(controller.getInterface());
  expect(fromComponent != nullptr && fromController != nullptr,
         "host can discover metadata connection from both interfaces");
  FUnknownPtr<FUnknown> componentIdentity(component.getInterface());
  FUnknownPtr<FUnknown> controllerIdentity(controller.getInterface());
  FUnknownPtr<FUnknown> connectionIdentity(fromComponent.getInterface());
  expect(componentIdentity.getInterface() == controllerIdentity.getInterface() &&
             componentIdentity.getInterface() == connectionIdentity.getInterface(),
         "metadata interface preserves the single component identity");
  TUID controllerClass{};
  expect(component->getControllerClassId(controllerClass) == kNotImplemented,
         "no separate controller class is introduced");
  const auto initial = readSnapshot(*processor);
  expect(revisionOf(initial) > 0 && initial["nowPlaying"].isVoid(),
         "ordinary hosts start with an empty metadata snapshot");
  auto unknown = makeMessage("Unrelated.Host.Message");
  expect(fromComponent->notify(unknown.get()) == kResultFalse,
         "unknown messages retain the inherited response");
  auto text = makeMessage("TextMessage");
  text->getAttributes()->setString("Text", STR16("SDK base notification"));
  expect(fromComponent->notify(text.get()) == kResultOk,
         "inherited SDK text notifications still work");
  expect(revisionOf(readSnapshot(*processor)) == revisionOf(initial),
         "unrelated notifications leave metadata untouched");
  expectRejected(*processor, nullptr, "null message is rejected");
  auto missingId = owned(new IncompleteMessage(nullptr));
  expectRejected(*processor, missingId.get(), "missing message ID is rejected");
  auto missingAttributes = owned(new IncompleteMessage("EffeTune.NowPlaying"));
  expect(processor->notify(missingAttributes.get()) == kResultOk &&
             readSnapshot(*processor)["nowPlaying"].isVoid(),
         "recognized message without an attribute list clears metadata");
  auto peer = makeProcessor();
  FUnknownPtr<IConnectionPoint> peerConnection(static_cast<IComponent *>(peer.get()));
  expect(fromComponent->connect(nullptr) == kInvalidArgument,
         "inherited null connection rejection survives");
  expect(fromComponent->connect(peerConnection) == kResultOk,
         "inherited connection still accepts a peer");
  expect(fromComponent->connect(peerConnection) == kResultFalse,
         "inherited connection still refuses a second peer");
  expect(fromComponent->disconnect(peerConnection) == kResultOk,
         "inherited disconnect still releases the peer");
  expect(fromComponent->disconnect(peerConnection) == kResultFalse,
         "inherited disconnect still reports an absent peer");
  expect(peer->terminate() == kResultOk, "terminate peer");
  expect(processor->terminate() == kResultOk, "terminate ordinary host processor");
}

void testReplacementOwnershipAndPolling() {
  auto processor = makeProcessor();
  auto isolated = makeProcessor();
  {
    auto message = makeMessage();
    auto *attributes = message->getAttributes();
    attributes->setString("title", STR16("日本語 \U0001f3b5 <Title>"));
    attributes->setString("album", STR16("Album & \"quoted\""));
    attributes->setString("artist", STR16("Artist"));
    attributes->setString("artworkMimeType", STR16("image/png"));
    const std::array<std::uint8_t, 3> bytes{1, 2, 3};
    attributes->setBinary("artwork", bytes.data(), static_cast<uint32>(bytes.size()));
    expect(processor->notify(message.get()) == kResultOk, "accept complete snapshot");
    attributes->setString("title", STR16("Mutated host message"));
  }
  const auto snapshot = readSnapshot(*processor);
  const auto metadata = snapshot["nowPlaying"];
  expect(metadata["title"].getWithDefault<std::string>({}) == "日本語 🎵 <Title>",
         "UTF16 text is copied and converted without markup interpretation");
  expect(metadata["album"].getWithDefault<std::string>({}) == "Album & \"quoted\"" &&
             metadata["artist"].getWithDefault<std::string>({}) == "Artist",
         "all text fields survive host message destruction");
  expect(metadata["artwork"][0]["src"].getWithDefault<std::string>({}) ==
             "data:image/png;base64,AQID", "owned binary reaches the UI data URL");
  expect(readSnapshot(*isolated)["nowPlaying"].isVoid(), "metadata is instance-local");
  expect(snapshotJson(*processor) == choc::json::toString(metadata),
         "a startup or reopened editor receives the cached snapshot again");
  const auto matched = readSnapshot(*processor, "telemetry/read", revisionOf(snapshot));
  expect(revisionOf(matched) == revisionOf(snapshot) &&
             !matched.hasObjectMember("nowPlaying"),
         "unchanged telemetry omits the artwork payload");
  const auto replay = readSnapshot(*processor, "telemetry/read");
  expect(choc::json::toString(replay["nowPlaying"]) == choc::json::toString(metadata),
         "another reader can replay metadata without a global drain");

  ResizableMemoryIBStream state;
  expect(processor->getState(&state) == kResultOk, "save ordinary VST state");
  const std::string encodedState(static_cast<const char *>(state.getData()), state.getCursor());
  expect(encodedState.find("nowPlaying") == std::string::npos &&
             encodedState.find("data:image/png") == std::string::npos &&
             encodedState.find("<Title>") == std::string::npos,
         "transient host metadata is excluded from serialized state");
  state.rewind();
  expect(processor->setState(&state) == kResultOk, "restore ordinary VST state");
  expect(snapshotJson(*processor) == choc::json::toString(metadata),
         "pipeline state restoration preserves the current host snapshot");

  auto replacement = makeMessage();
  replacement->getAttributes()->setString("title", STR16("Next track"));
  expect(processor->notify(replacement.get()) == kResultOk, "accept title-only replacement");
  const auto next = readSnapshot(*processor);
  expect(revisionOf(next) > revisionOf(snapshot) &&
             next["nowPlaying"]["title"].getWithDefault<std::string>({}) == "Next track" &&
             next["nowPlaying"]["album"].getWithDefault<std::string>({}).empty() &&
             next["nowPlaying"]["artist"].getWithDefault<std::string>({}).empty() &&
             next["nowPlaying"]["artwork"].size() == 0,
         "full replacement clears every omitted field");
  auto clear = makeMessage();
  expect(processor->notify(clear.get()) == kResultOk, "empty attributes clear metadata");
  expect(readSnapshot(*processor)["nowPlaying"].isVoid(), "clear reaches the renderer");
  expect(processor->notify(replacement.get()) == kResultOk, "restore metadata before null-list clear");
  auto missingAttributes = owned(new IncompleteMessage("EffeTune.NowPlaying"));
  expect(processor->notify(missingAttributes.get()) == kResultOk &&
             readSnapshot(*processor)["nowPlaying"].isVoid(),
         "absent attribute list clears an existing snapshot");
  expect(isolated->terminate() == kResultOk, "terminate isolated processor");
  expect(processor->terminate() == kResultOk, "terminate snapshot processor");
}

void testAtomicTransportValidation() {
  auto processor = makeProcessor();
  auto baseline = makeMessage();
  baseline->getAttributes()->setString("title", STR16("Keep this track"));
  expect(processor->notify(baseline.get()) == kResultOk, "install rejection baseline");
  auto invalid = makeMessage();
  invalid->getAttributes()->setString("title", STR16("Must not replace baseline"));
  const std::array<std::uint8_t, 3> bytes{1, 2, 3};
  invalid->getAttributes()->setBinary("artwork", bytes.data(), static_cast<uint32>(bytes.size()));
  expectRejected(*processor, invalid.get(), "artwork without MIME is rejected");
  invalid->getAttributes()->setString("artworkMimeType", STR16("image/svg+xml"));
  expectRejected(*processor, invalid.get(), "unsupported artwork MIME is rejected");
  invalid->getAttributes()->setString("artworkMimeType", STR16("image/png"));
  const std::vector<std::uint8_t> tooLarge(8 * 1024 * 1024 + 1, 0);
  invalid->getAttributes()->setBinary("artwork", tooLarge.data(),
                                      static_cast<uint32>(tooLarge.size()));
  expectRejected(*processor, invalid.get(), "oversized artwork is rejected atomically");
  auto text = makeMessage();
  const std::u16string maximum(4096, u'x');
  text->getAttributes()->setString("title", maximum.c_str());
  expect(processor->notify(text.get()) == kResultOk, "4096 text code units are accepted");
  const std::u16string tooLong(4097, u'x');
  text->getAttributes()->setString("title", tooLong.c_str());
  expectRejected(*processor, text.get(), "text beyond 4096 code units is rejected");
  const std::array<TChar, 2> unpairedSurrogate{static_cast<TChar>(0xd800), 0};
  text->getAttributes()->setString("title", unpairedSurrogate.data());
  expectRejected(*processor, text.get(), "invalid UTF16 is rejected");
  text->getAttributes()->setInt("title", 123);
  expectRejected(*processor, text.get(), "incorrect known attribute type is rejected");
  for (const auto *mime : {STR16("image/png"), STR16("image/jpeg"),
                           STR16("image/gif"), STR16("image/webp")}) {
    auto image = makeMessage();
    image->getAttributes()->setString("artworkMimeType", mime);
    image->getAttributes()->setBinary("artwork", bytes.data(), static_cast<uint32>(bytes.size()));
    expect(processor->notify(image.get()) == kResultOk,
           "supported MIME accepts opaque binary without native image decoding");
  }
  expect(processor->terminate() == kResultOk, "terminate validation processor");
  expect(readSnapshot(*processor)["nowPlaying"].isVoid(), "terminate clears the transient snapshot");
}

void testHostLyricsMetadata() {
  auto processor = makeProcessor();
  auto message = makeMessage();
  auto *attributes = message->getAttributes();
  attributes->setInt("mediaGeneration", -1); // All u64 bits survive the VST signed carrier.
  const std::u16string maximum(262142, u'x');
  const auto lyrics = maximum + u"\U0001f3b5";
  attributes->setString("lyricsSidecar", lyrics.c_str());
  attributes->setString("lyricsEmbedded", lyrics.c_str());
  attributes->setFloat("durationSeconds", 60.0);
  expect(processor->notify(message.get()) == kResultOk, "accept both maximum lyric strings without artwork or title");
  const auto snapshot = readSnapshot(*processor);
  const auto metadata = snapshot["nowPlaying"];
  expect(metadata["mediaGeneration"].get<std::string>() == "18446744073709551615",
         "UI bridge preserves all generation bits");
  expect(metadata["lyricsSidecar"].get<std::string>().size() == 262146 &&
         metadata["lyricsEmbedded"].get<std::string>().size() == 262146 &&
         metadata["durationSeconds"].get<double>() == 60.0, "own both lyric strings and duration");
  expect(!readSnapshot(*processor, "telemetry/read", revisionOf(snapshot)).hasObjectMember("nowPlaying"),
         "unchanged revision omits lyrics too");
  for (const auto *key : {"lyricsSidecar", "lyricsEmbedded"}) {
    auto invalid = makeMessage();
    const std::u16string tooLong(262145, u'x');
    invalid->getAttributes()->setString(key, tooLong.c_str());
    expectRejected(*processor, invalid.get(), "over-limit lyrics preserve snapshot");
    const std::array<TChar, 2> surrogate{static_cast<TChar>(0xdc00), 0};
    invalid->getAttributes()->setString(key, surrogate.data());
    expectRejected(*processor, invalid.get(), "ill-formed lyrics preserve snapshot");
    invalid->getAttributes()->setInt(key, 1);
    expectRejected(*processor, invalid.get(), "wrong lyric type preserves snapshot");
  }
  for (const auto value : {-1.0, std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()}) {
    auto invalid = makeMessage();
    invalid->getAttributes()->setFloat("durationSeconds", value);
    expectRejected(*processor, invalid.get(), "invalid duration preserves snapshot");
  }
  for (const auto *key : {"durationSeconds", "mediaGeneration"}) {
    auto invalid = makeMessage();
    invalid->getAttributes()->setString(key, STR16("wrong type"));
    expectRejected(*processor, invalid.get(), "wrong optional scalar type preserves snapshot");
  }
  auto replacement = makeMessage();
  replacement->getAttributes()->setString("title", STR16("No lyrics"));
  expect(processor->notify(replacement.get()) == kResultOk, "accept legacy replacement");
  const auto cleared = choc::value::Value(readSnapshot(*processor)["nowPlaying"]);
  expect(cleared["lyricsSidecar"].get<std::string>().empty() &&
         cleared["lyricsEmbedded"].get<std::string>().empty() &&
         !cleared.hasObjectMember("mediaGeneration") && !cleared.hasObjectMember("durationSeconds"),
         "replacement clears all omitted lyrics attributes");
  expect(processor->terminate() == kResultOk, "terminate host lyrics processor");
}

void testPlaybackPosition() {
  using effetune::vst::plugin::PluginProcessorTestAccess;
  auto processor = makeProcessor();
  expect(readSnapshot(*processor)["playback"]["available"].get<int>() == 0, "ordinary hosts have no position");
  auto message = makeMessage("EffeTune.PlaybackPosition");
  auto *attributes = message->getAttributes();
  attributes->setInt("mediaGeneration", 42);
  attributes->setInt("available", 1);
  attributes->setInt("playing", 1);
  attributes->setFloat("positionSeconds", 10.0);
  attributes->setFloat("rate", 2.0);
  attributes->setInt("hostTimeNanoseconds", 1000000000);
  expect(processor->notify(message.get()) == kResultOk, "accept playing position");
  expect(PluginProcessorTestAccess::playbackAt(*processor, 2500000000)["positionSeconds"].get<double>() == 13.0,
         "native extrapolation uses supplied clock and rate without subtracting latency");
  const auto before = choc::json::toString(PluginProcessorTestAccess::playbackAt(*processor, 2500000000));
  const auto reject = [&](IMessage *invalid) {
    expect(processor->notify(invalid) == kInvalidArgument, "reject invalid position");
    expect(choc::json::toString(PluginProcessorTestAccess::playbackAt(*processor, 2500000000)) == before,
           "invalid position retains complete previous snapshot");
  };
  auto incomplete = makeMessage("EffeTune.PlaybackPosition");
  reject(incomplete.get());
  auto noList = owned(new IncompleteMessage("EffeTune.PlaybackPosition"));
  reject(noList.get());
  for (const auto *key : {"mediaGeneration", "available", "playing", "positionSeconds", "rate", "hostTimeNanoseconds"}) {
    auto invalid = makeMessage("EffeTune.PlaybackPosition");
    auto *fields = invalid->getAttributes();
    fields->setInt("mediaGeneration", 42); fields->setInt("available", 1); fields->setInt("playing", 1);
    fields->setFloat("positionSeconds", 10.0); fields->setFloat("rate", 2.0);
    fields->setInt("hostTimeNanoseconds", 1000000000);
    fields->setString(key, STR16("wrong type"));
    reject(invalid.get());
  }
  for (const auto *key : {"positionSeconds", "rate"}) {
    const auto original = std::string_view(key) == "rate" ? 2.0 : 10.0;
    for (const auto value : {-1.0, std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()}) {
      attributes->setFloat(key, value); reject(message.get());
    }
    attributes->setFloat(key, original);
  }
  attributes->setFloat("rate", 0.0); reject(message.get()); attributes->setFloat("rate", 2.0);
  attributes->setInt("available", 2); reject(message.get()); attributes->setInt("available", 1);
  attributes->setInt("playing", 2); reject(message.get()); attributes->setInt("playing", 1);
  attributes->setInt("hostTimeNanoseconds", -1); reject(message.get());
  attributes->setInt("hostTimeNanoseconds", 1000000000);
  attributes->setInt("playing", 0);
  expect(processor->notify(message.get()) == kResultOk, "accept paused position");
  expect(PluginProcessorTestAccess::playbackAt(*processor, 9000000000)["positionSeconds"].get<double>() == 10.0,
         "paused position never advances");
  const auto host = choc::value::Value(readSnapshot(*processor)["playback"]);
  const auto telemetry = choc::value::Value(readSnapshot(*processor, "telemetry/read")["playback"]);
  expect(choc::json::toString(host) == choc::json::toString(telemetry) &&
         host["mediaGeneration"].get<std::string>() == "42", "both routes carry the same playback snapshot");
  attributes->setInt("mediaGeneration", 43);
  attributes->setInt("available", 0);
  attributes->setString("rate", STR16("ignored while unavailable"));
  expect(processor->notify(message.get()) == kResultOk, "unavailable ignores other position fields");
  const auto unavailable = choc::value::Value(readSnapshot(*processor)["playback"]);
  expect(unavailable["available"].get<int>() == 0 && unavailable["positionSeconds"].get<double>() == 0.0 &&
         unavailable["mediaGeneration"].get<std::string>() == "43", "unavailable removes old position");
  expect(processor->terminate() == kResultOk, "terminate playback processor");
  expect(!readSnapshot(*processor)["playback"].hasObjectMember("mediaGeneration"), "termination clears position");
}

void testZeroPositionAndGeneration() {
  using effetune::vst::plugin::PluginProcessorTestAccess;
  auto processor = makeProcessor();
  auto metadata = makeMessage();
  metadata->getAttributes()->setInt("mediaGeneration", 0);
  metadata->getAttributes()->setFloat("durationSeconds", 0.0);
  expect(processor->notify(metadata.get()) == kResultOk, "accept zero metadata generation and duration");

  auto message = makeMessage("EffeTune.PlaybackPosition");
  auto *attributes = message->getAttributes();
  attributes->setInt("mediaGeneration", 0);
  attributes->setInt("available", 1);
  attributes->setInt("playing", 0);
  attributes->setFloat("positionSeconds", 0.0);
  attributes->setFloat("rate", 1.0);
  attributes->setInt("hostTimeNanoseconds", 0);
  expect(processor->notify(message.get()) == kResultOk, "accept zero position, generation and host time");
  for (const auto *route : {"host/getInfo", "telemetry/read"}) {
    const auto snapshot = readSnapshot(*processor, route);
    expect(snapshot["nowPlaying"]["mediaGeneration"].get<std::string>() == "0" &&
               snapshot["nowPlaying"]["durationSeconds"].get<double>() == 0.0 &&
               snapshot["playback"]["mediaGeneration"].get<std::string>() == "0" &&
               snapshot["playback"]["available"].get<int>() == 1 &&
               snapshot["playback"]["positionSeconds"].get<double>() == 0.0,
           "both routes retain zero values rather than treating them as omitted");
  }
  attributes->setInt("playing", 1);
  expect(processor->notify(message.get()) == kResultOk, "accept advancing position at zero");
  expect(PluginProcessorTestAccess::playbackAt(*processor, 1000000000)["positionSeconds"].get<double>() == 1.0,
         "zero position and host time are valid extrapolation origins");
  expect(processor->terminate() == kResultOk, "terminate zero boundary processor");
}

void testHighBitGenerationAndRepeatedNotifications() {
  using effetune::vst::plugin::PluginProcessorTestAccess;
  auto processor = makeProcessor();
  constexpr auto generation = std::numeric_limits<int64>::min();
  constexpr auto generationText = "9223372036854775808";
  auto metadata = makeMessage();
  metadata->getAttributes()->setInt("mediaGeneration", generation);
  metadata->getAttributes()->setString("lyricsSidecar", STR16("[00:00.00]First"));
  expect(processor->notify(metadata.get()) == kResultOk, "accept the high generation bit in metadata");
  const auto first = readSnapshot(*processor);
  expect(first["nowPlaying"]["mediaGeneration"].get<std::string>() == generationText,
         "metadata preserves the high bit as an unsigned decimal string");
  metadata->getAttributes()->setString("lyricsSidecar", STR16("[00:00.00]Updated"));
  expect(processor->notify(metadata.get()) == kResultOk, "accept updated lyrics for the same generation");
  const auto updated = readSnapshot(*processor, "telemetry/read", revisionOf(first));
  expect(revisionOf(updated) > revisionOf(first) &&
             updated["nowPlaying"]["mediaGeneration"].get<std::string>() == generationText &&
             updated["nowPlaying"]["lyricsSidecar"].get<std::string>() == "[00:00.00]Updated",
         "same-generation metadata is republished with its new revision");
  expect(processor->notify(metadata.get()) == kResultOk, "accept an identical metadata notification");
  const auto repeated = readSnapshot(*processor, "telemetry/read", revisionOf(updated));
  expect(revisionOf(repeated) > revisionOf(updated) &&
             choc::json::toString(repeated["nowPlaying"]) == choc::json::toString(updated["nowPlaying"]),
         "identical metadata is republished without losing its generation or lyrics");

  auto message = makeMessage("EffeTune.PlaybackPosition");
  auto *attributes = message->getAttributes();
  attributes->setInt("mediaGeneration", generation);
  attributes->setInt("available", 1);
  attributes->setInt("playing", 0);
  attributes->setFloat("positionSeconds", 10.0);
  attributes->setFloat("rate", 1.0);
  attributes->setInt("hostTimeNanoseconds", 0);
  expect(processor->notify(message.get()) == kResultOk, "accept the high generation bit in playback");
  expect(PluginProcessorTestAccess::playbackAt(*processor, 0)["mediaGeneration"].get<std::string>() == generationText,
         "playback preserves the high generation bit too");
  attributes->setFloat("positionSeconds", 3.0);
  attributes->setFloat("rate", 0.5);
  expect(processor->notify(message.get()) == kResultOk, "accept a seek within the same generation");
  const auto playback = PluginProcessorTestAccess::playbackAt(*processor, 0);
  expect(playback["mediaGeneration"].get<std::string>() == generationText &&
             playback["positionSeconds"].get<double>() == 3.0 && playback["rate"].get<double>() == 0.5,
         "same-generation playback replaces position and rate");
  expect(processor->notify(message.get()) == kResultOk, "accept an identical playback notification");
  expect(choc::json::toString(PluginProcessorTestAccess::playbackAt(*processor, 0)) ==
             choc::json::toString(playback), "identical notification preserves the complete position");
  expect(processor->terminate() == kResultOk, "terminate high-bit generation processor");
}

void testPlaybackExtrapolationBounds() {
  using effetune::vst::plugin::PluginProcessorTestAccess;
  auto processor = makeProcessor();
  auto message = makeMessage("EffeTune.PlaybackPosition");
  auto *attributes = message->getAttributes();
  attributes->setInt("mediaGeneration", 1);
  attributes->setInt("available", 1);
  attributes->setInt("playing", 1);
  attributes->setFloat("positionSeconds", 1.0);
  attributes->setFloat("rate", 2.0);
  attributes->setInt("hostTimeNanoseconds", 2000000000);
  expect(processor->notify(message.get()) == kResultOk, "accept a position ahead of the response clock");
  expect(PluginProcessorTestAccess::playbackAt(*processor, 1750000000)["positionSeconds"].get<double>() == 0.5,
         "negative elapsed time extrapolates backwards while position remains positive");
  expect(PluginProcessorTestAccess::playbackAt(*processor, 1500000000)["positionSeconds"].get<double>() == 0.0 &&
             PluginProcessorTestAccess::playbackAt(*processor, 1000000000)["positionSeconds"].get<double>() == 0.0,
         "backwards extrapolation clamps at zero and below zero");

  constexpr auto maximum = std::numeric_limits<double>::max();
  attributes->setFloat("positionSeconds", maximum);
  attributes->setFloat("rate", maximum);
  attributes->setInt("hostTimeNanoseconds", 0);
  expect(processor->notify(message.get()) == kResultOk, "accept finite maximum position and rate");
  expect(PluginProcessorTestAccess::playbackAt(*processor, 2000000000)["positionSeconds"].get<double>() == maximum,
         "overflowing extrapolation saturates at the largest finite double");
  for (const auto *route : {"host/getInfo", "telemetry/read"}) {
    const auto snapshot = readSnapshot(*processor, route);
    const auto position = snapshot["playback"]["positionSeconds"].get<double>();
    expect(std::isfinite(position) && position == maximum,
           "both JSON routes preserve a finite saturated position");
  }
  expect(processor->terminate() == kResultOk, "terminate extrapolation boundary processor");
}

} // namespace

int runTestBody() {
  effetune::vst::testing::suppressCrtModalDialogs();
  try {
    effetune::vst::testing::checkWebViewTestDeadline();
    testInterfacesAndOrdinaryHost();
    effetune::vst::testing::checkWebViewTestDeadline();
    testReplacementOwnershipAndPolling();
    effetune::vst::testing::checkWebViewTestDeadline();
    testAtomicTransportValidation();
    effetune::vst::testing::checkWebViewTestDeadline();
    testHostLyricsMetadata();
    effetune::vst::testing::checkWebViewTestDeadline();
    testPlaybackPosition();
    effetune::vst::testing::checkWebViewTestDeadline();
    testZeroPositionAndGeneration();
    effetune::vst::testing::checkWebViewTestDeadline();
    testHighBitGenerationAndRepeatedNotifications();
    effetune::vst::testing::checkWebViewTestDeadline();
    testPlaybackExtrapolationBounds();
    std::cout << "EffeTune Now Playing metadata tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Test failure: " << error.what() << '\n';
    return 1;
  }
}

int main() {
  return effetune::vst::testing::runWebViewTest([] { return runTestBody(); },
      std::chrono::seconds(120), effetune::vst::pendingWebViewCallbacksForTesting);
}
