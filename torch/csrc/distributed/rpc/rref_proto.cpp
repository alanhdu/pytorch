#include <torch/csrc/distributed/rpc/rref_proto.h>
#include <torch/csrc/jit/pickle.h>

#include <limits>

namespace torch {
namespace distributed {
namespace rpc {

namespace {

std::vector<IValue> toIValues(const Message& message, MessageType type) {
  TORCH_INTERNAL_ASSERT(
      type == message.type(),
      "Expecting message of type ",
      type,
      ", but got ",
      message.type());
  auto payload = static_cast<const char*>(message.payload().data());
  auto payload_size = message.payload().size();

  auto value =
      jit::unpickle(payload, payload_size, nullptr, &message.tensors());
  return value.toTuple()->elements();
}

Message fromIValues(std::vector<IValue> ivalues, MessageType type) {
  std::vector<torch::Tensor> tensor_table;
  auto payload = jit::pickle(
      c10::ivalue::Tuple::create(std::move(ivalues)), &tensor_table);
  return Message(std::move(payload), std::move(tensor_table), type);
}

} // namespace

/////////////////////////// RRefMessageBase //////////////////////////////////

const RRefId& RRefMessageBase::rrefId() {
  return rrefId_;
}

Message RRefMessageBase::toMessage() const {
  return fromIValues({rrefId_.toIValue()}, type_);
}

at::IValue RRefMessageBase::fromMessage(
    const Message& message,
    MessageType type) {
  auto values = toIValues(message, type);

  TORCH_INTERNAL_ASSERT(
      values.size() == 1, "ScriptUserDelete expects 1 IValue from message.");
  return std::move(values.back());
}

/////////////////////////// ForkMessageBase //////////////////////////////////

const ForkId& ForkMessageBase::forkId() {
  return forkId_;
}

Message ForkMessageBase::toMessage() const {
  return fromIValues({rrefId_.toIValue(), forkId_.toIValue()}, type_);
}

std::pair<RRefId, ForkId> ForkMessageBase::fromMessage(
    const Message& message,
    MessageType type) {
  auto ivalues = toIValues(message, type);

  TORCH_INTERNAL_ASSERT(
      ivalues.size() == 2, "ScriptUserDelete expects 2 IValue from message.");

  return std::make_pair(
      RRefId::fromIValue(ivalues[0]), ForkId::fromIValue(ivalues[1]));
}

/////////////////////////// RRef Protocol //////////////////////////////////

ScriptRRefFetchCall ScriptRRefFetchCall::fromMessage(const Message& message) {
  return ScriptRRefFetchCall(RRefId::fromIValue(RRefMessageBase::fromMessage(
      message, MessageType::SCRIPT_RREF_FETCH_CALL)));
}

PythonRRefFetchCall PythonRRefFetchCall::fromMessage(const Message& message) {
  return PythonRRefFetchCall(RRefId::fromIValue(RRefMessageBase::fromMessage(
      message, MessageType::PYTHON_RREF_FETCH_CALL)));
}

const at::IValue& RRefFetchRet::value() {
  return value_;
}

Message RRefFetchRet::toMessage() const {
  std::vector<at::IValue> ivalues;
  ivalues.emplace_back(value_);
  std::vector<torch::Tensor> tensor_table;
  auto payload =
      jit::pickle(c10::ivalue::Tuple::create(ivalues), &tensor_table);

  return Message(
      std::move(payload), std::move(tensor_table), MessageType::RREF_FETCH_RET);
}

RRefFetchRet RRefFetchRet::fromMessage(const Message& message) {
  auto payload = static_cast<const char*>(message.payload().data());
  auto payload_size = message.payload().size();

  auto value =
      jit::unpickle(payload, payload_size, nullptr, &message.tensors());
  auto values = value.toTuple()->elements();

  AT_ASSERT(values.size() == 1, "Expect 1 IValue from message.");
  return RRefFetchRet(values.front());
}

RRefUserDelete RRefUserDelete::fromMessage(const Message& message) {
  auto pair =
      ForkMessageBase::fromMessage(message, MessageType::RREF_USER_DELETE);
  return RRefUserDelete(pair.first, pair.second);
}

RRefUserAccept RRefUserAccept::fromMessage(const Message& message) {
  auto pair =
      ForkMessageBase::fromMessage(message, MessageType::RREF_USER_ACCEPT);
  return RRefUserAccept(pair.first, pair.second);
}

RemoteRet RemoteRet::fromMessage(const Message& message) {
  auto pair = ForkMessageBase::fromMessage(message, MessageType::REMOTE_RET);
  return RemoteRet(pair.first, pair.second);
}

worker_id_t RRefForkNotify::forkDst() const {
  return forkDst_;
}

Message RRefForkNotify::toMessage() const {
  return fromIValues(
      {rrefId_.toIValue(), forkId_.toIValue(), IValue(forkDst_)},
      MessageType::RREF_FORK_NOTIFY);
}

RRefForkNotify RRefForkNotify::fromMessage(const Message& message) {
  auto values = toIValues(message, MessageType::RREF_FORK_NOTIFY);

  AT_ASSERT(values.size() == 3, "Expect 3 IValues from message.");
  auto forkDst = values.back().toInt();
  AT_ASSERT(
      forkDst < std::numeric_limits<worker_id_t>::max(),
      "Fork destination worker id out of bound ",
      forkDst);
  values.pop_back();
  RRefId rrefId = RRefId::fromIValue(values.back());
  values.pop_back();
  ForkId forkId = ForkId::fromIValue(values.back());

  return RRefForkNotify(rrefId, forkId, forkDst);
}

const ForkId& RRefForkAccept::forkId() const {
  return forkId_;
}

Message RRefForkAccept::toMessage() {
  return fromIValues({forkId_.toIValue()}, MessageType::RREF_FORK_ACCEPT);
}

RRefForkAccept RRefForkAccept::fromMessage(const Message& message) {
  auto values = toIValues(message, MessageType::RREF_FORK_ACCEPT);
  AT_ASSERT(values.size() == 1, "Expect 1 IValues from message.");

  return RRefForkAccept(ForkId::fromIValue(values.back()));
}

} // namespace rpc
} // namespace distributed
} // namespace torch
