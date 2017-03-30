#include <vector>
#include "nan.h"
#include "spellchecker.h"

using Nan::ObjectWrap;
using namespace spellchecker;
using namespace v8;
using Nan::AsyncQueueWorker;
using Nan::AsyncWorker;
using Nan::Callback;
using Nan::New;
using Nan::Null;
using Nan::To;

namespace {

class CheckSpellingWorker : public AsyncWorker {
  public:
    CheckSpellingWorker(Callback *callback, SpellcheckerImplementation *impl, std::vector<uint16_t>& text)
        : AsyncWorker(callback), impl(impl), text(text) {}
    ~CheckSpellingWorker() {}

    void Execute()
    {
      misspelled_ranges = impl->CheckSpelling(text.data(), text.size());
    }

    // Executed when the async work is complete
    // this function will be run inside the main event loop
    // so it is safe to use V8 again
    void HandleOKCallback()
    {
      HandleScope scope;

      Local<Array> misspelledRanges = Nan::New<Array>();
      std::vector<MisspelledRange>::const_iterator iter = misspelled_ranges.begin();
      for (; iter != misspelled_ranges.end(); ++iter)
      {
        size_t index = iter - misspelled_ranges.begin();
        uint32_t start = iter->start, end = iter->end;

        Local<Object> misspelled_range = Nan::New<Object>();
        misspelled_range->Set(Nan::New("start").ToLocalChecked(), Nan::New<Integer>(start));
        misspelled_range->Set(Nan::New("end").ToLocalChecked(), Nan::New<Integer>(end));
        misspelledRanges->Set(index, misspelled_range);
      }
      
      Local<Value> argv[] = {
          Null(), misspelledRanges};

      callback->Call(2, argv);
    }

    private:
      SpellcheckerImplementation *impl;
      std::vector<uint16_t> text;
      std::vector<MisspelledRange> misspelled_ranges;
};

class GetCorrectionsForMisspellingWorker : public AsyncWorker {
  public:
    GetCorrectionsForMisspellingWorker(Callback *callback, SpellcheckerImplementation *impl, std::string& word)
        : AsyncWorker(callback), impl(impl), word(word) {}
    ~GetCorrectionsForMisspellingWorker() {}

    void Execute()
    {
      corrections = impl->GetCorrectionsForMisspelling(word);
    }

    void HandleOKCallback()
    {
      HandleScope scope;

      Local<Array> result = Nan::New<Array>(corrections.size());
      for (size_t i = 0; i < corrections.size(); ++i)
      {
        const std::string &word = corrections[i];

        Nan::MaybeLocal<String> val = Nan::New<String>(word.data(), word.size());
        result->Set(i, val.ToLocalChecked());
      }

      Local<Value> argv[] = {
          Null(), result};

      callback->Call(2, argv);
    }

    private:
      SpellcheckerImplementation *impl;
      std::string word;
      std::vector<std::string> corrections;
};

class Spellchecker : public Nan::ObjectWrap {
  SpellcheckerImplementation* impl;
  v8::Persistent<v8::Value> dictData;

  static NAN_METHOD(New) {
    Nan::HandleScope scope;
    Spellchecker* that = new Spellchecker();
    that->Wrap(info.This());

    info.GetReturnValue().Set(info.This());
  }

  static NAN_METHOD(SetDictionary) {
    Nan::HandleScope scope;

    if (info.Length() < 1) {
      return Nan::ThrowError("Bad argument");
    }
    
    Spellchecker* that = Nan::ObjectWrap::Unwrap<Spellchecker>(info.Holder());
    
    bool has_contents = false; 
    if (info.Length() > 1) {
      if (!node::Buffer::HasInstance(info[1])) {
        return Nan::ThrowError("SetDictionary 2nd argument must be a Buffer");
      }
      
      // NB: We must guarantee that we pin this Buffer
      that->dictData.Reset(info.GetIsolate(), info[1]);
      has_contents = true;
    }

    std::string language = *String::Utf8Value(info[0]);
    
    bool result;
    if (has_contents) {
      result = that->impl->SetDictionaryToContents(
        (unsigned char*)node::Buffer::Data(info[1]), 
        node::Buffer::Length(info[1]));
    } else {
      result = that->impl->SetDictionary(language);
    }
    
    info.GetReturnValue().Set(Nan::New(result));
  }

  static NAN_METHOD(IsMisspelled) {
    Nan::HandleScope scope;
    if (info.Length() < 1) {
      return Nan::ThrowError("Bad argument");
    }

    Spellchecker* that = Nan::ObjectWrap::Unwrap<Spellchecker>(info.Holder());
    std::string word = *String::Utf8Value(info[0]);

    info.GetReturnValue().Set(Nan::New(that->impl->IsMisspelled(word)));
  }

  static NAN_METHOD(CheckSpellingAsync) {
    Nan::HandleScope scope;
    if (info.Length() < 1) {
      return Nan::ThrowError("Bad argument");
    }

    Handle<String> string = Handle<String>::Cast(info[0]);
    if (!string->IsString()) {
      return Nan::ThrowError("Bad argument");
    }

    if (string->Length() == 0)
    {
      return;
    }
    std::vector<uint16_t> text(string->Length() + 1);
    string->Write(reinterpret_cast<uint16_t *>(text.data()));

    Callback *callback = new Callback(info[1].As<Function>());
    Spellchecker *that = Nan::ObjectWrap::Unwrap<Spellchecker>(info.Holder());
    AsyncQueueWorker(new CheckSpellingWorker(callback, that->impl, text));
  }

  static NAN_METHOD(Add) {
    Nan::HandleScope scope;
    if (info.Length() < 1) {
      return Nan::ThrowError("Bad argument");
    }

    Spellchecker* that = Nan::ObjectWrap::Unwrap<Spellchecker>(info.Holder());
    std::string word = *String::Utf8Value(info[0]);

    that->impl->Add(word);
    return;
  }
  
  static NAN_METHOD(Remove) {
    Nan::HandleScope scope;
    if (info.Length() < 1) {
      return Nan::ThrowError("Bad argument");
    }

    Spellchecker* that = Nan::ObjectWrap::Unwrap<Spellchecker>(info.Holder());
    std::string word = *String::Utf8Value(info[0]);

    that->impl->Remove(word);
    return;
  }


  static NAN_METHOD(GetAvailableDictionaries) {
    Nan::HandleScope scope;

    Spellchecker* that = Nan::ObjectWrap::Unwrap<Spellchecker>(info.Holder());

    std::string path = ".";
    if (info.Length() > 0) {
      std::string path = *String::Utf8Value(info[0]);
    }

    std::vector<std::string> dictionaries =
      that->impl->GetAvailableDictionaries(path);

    Local<Array> result = Nan::New<Array>(dictionaries.size());
    for (size_t i = 0; i < dictionaries.size(); ++i) {
      const std::string& dict = dictionaries[i];
      result->Set(i, Nan::New(dict.data(), dict.size()).ToLocalChecked());
    }

    info.GetReturnValue().Set(result);
  }

  static NAN_METHOD(GetCorrectionsForMisspellingAsync)
  {
    Nan::HandleScope scope;
    if (info.Length() < 1) {
      return Nan::ThrowError("Bad argument");
    }

    Spellchecker* that = Nan::ObjectWrap::Unwrap<Spellchecker>(info.Holder());
    std::string word = *String::Utf8Value(info[0]);
    Callback *callback = new Callback(info[1].As<Function>());
    AsyncQueueWorker(new GetCorrectionsForMisspellingWorker(callback, that->impl, word));
  }

  Spellchecker() {
    impl = SpellcheckerFactory::CreateSpellchecker();
  }

  // actual destructor
  virtual ~Spellchecker() {
    delete impl;
  }

 public:
  static void Init(Handle<Object> exports) {
    Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(Spellchecker::New);

    tpl->SetClassName(Nan::New<String>("Spellchecker").ToLocalChecked());
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    Nan::SetMethod(tpl->InstanceTemplate(), "setDictionary", Spellchecker::SetDictionary);
    Nan::SetMethod(tpl->InstanceTemplate(), "getAvailableDictionaries", Spellchecker::GetAvailableDictionaries);
    Nan::SetMethod(tpl->InstanceTemplate(), "getCorrectionsForMisspellingAsync", Spellchecker::GetCorrectionsForMisspellingAsync);
    Nan::SetMethod(tpl->InstanceTemplate(), "isMisspelled", Spellchecker::IsMisspelled);
    Nan::SetMethod(tpl->InstanceTemplate(), "checkSpellingAsync", Spellchecker::CheckSpellingAsync);
    Nan::SetMethod(tpl->InstanceTemplate(), "add", Spellchecker::Add);
    Nan::SetMethod(tpl->InstanceTemplate(), "remove", Spellchecker::Remove);

    exports->Set(Nan::New("Spellchecker").ToLocalChecked(), tpl->GetFunction());
  }
};

void Init(Handle<Object> exports, Handle<Object> module) {
  Spellchecker::Init(exports);
}

}  // namespace

NODE_MODULE(spellchecker, Init)
