// labeltool: exercises Amp's label printing from the command line.
//   labeltool render <out.png> <title> <artist> <minutes> [artwork]   (no printer needed)
//   labeltool search
//   labeltool info <address>
//   labeltool print <address> <title> <artist> <minutes> [artwork]
#include "player/LabelPrinter.h"
#include "player/Messages.h"
#include "ui/LabelRenderer.h"

#include <Application.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <File.h>
#include <TranslationUtils.h>
#include <TranslatorRoster.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace amp;

namespace {

int Usage()
{
    fprintf(stderr, "usage: labeltool render <out.png> <title> <artist> <minutes> [artwork]\n"
        "       labeltool search\n"
        "       labeltool info <address>\n"
        "       labeltool print <address> <title> <artist> <minutes> [artwork]\n");
    return 1;
}

niimbot::Raster Render(char** argv, int argc, int first)
{
    LabelContent content;
    content.title = argv[first];
    content.artist = argv[first + 1];
    content.durationMs = (int64_t)(atof(argv[first + 2]) * 60000);
    BBitmap* art = argc > first + 3 ? BTranslationUtils::GetBitmap(argv[first + 3]) : nullptr;
    if (argc > first + 3 && art == nullptr)
        fprintf(stderr, "cannot read %s; the label goes without artwork\n", argv[first + 3]);
    content.artwork = art;
    niimbot::Raster raster = RenderLabel(content, DefaultLabelSize());
    delete art;
    return raster;
}

// Shows what the background jobs report until the final message.
class Reporter : public BLooper {
public:
    Reporter() : BLooper("reporter"), fDone(create_sem(0, "done")) {}
    void MessageReceived(BMessage* message) override
    {
        switch (message->what) {
            case kMsgLabelPrinterFound:
                printf("%s  %s  model %d  battery %d/4", message->GetString("address", ""),
                    message->GetString("name", ""), (int)message->GetInt32("model_id", 0),
                    (int)message->GetInt32("battery", -1));
                if (message->HasInt32("roll_total"))
                    printf("  roll %d of %d labels used", (int)message->GetInt32("roll_used", 0),
                        (int)message->GetInt32("roll_total", 0));
                printf("\n");
                if (fSingle)
                    release_sem(fDone);
                break;
            case kMsgLabelSearchDone:
                if (message->HasString("error"))
                    printf("failed: %s\n", message->GetString("error", ""));
                else
                    printf("search done: %d printer(s)\n", (int)message->GetInt32("count", 0));
                release_sem(fDone);
                break;
            case kMsgLabelProgress:
                printf("%3.0f%%  %s\n", message->GetFloat("fraction", 0) * 100, message->GetString("text", ""));
                break;
            case kMsgLabelPrinted:
                printf(message->GetBool("ok") ? "printed\n" : "failed: %s\n", message->GetString("error", ""));
                fOk = message->GetBool("ok");
                release_sem(fDone);
                break;
            default:
                BLooper::MessageReceived(message);
        }
    }
    void Wait() { acquire_sem(fDone); }
    bool fSingle = false;
    bool fOk = true;
private:
    sem_id fDone;
};

} // namespace

int main(int argc, char** argv)
{
    BApplication app("application/x-vnd.amp-labeltool");
    if (argc < 2)
        return Usage();
    const char* command = argv[1];
    if (strcmp(command, "render") == 0) {
        if (argc < 6)
            return Usage();
        niimbot::Raster raster = Render(argv, argc, 3);
        BBitmap* preview = RasterPreview(raster, 1);
        BBitmapStream stream(preview);
        BFile file(argv[2], B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
        status_t status = BTranslatorRoster::Default()->Translate(&stream, nullptr, nullptr, &file, B_PNG_FORMAT);
        printf("%s: %s (%zu row packets)\n", argv[2], strerror(status), niimbot::RowPackets(raster, 384).size());
        return status == B_OK ? 0 : 1;
    }
    Reporter* reporter = new Reporter();
    reporter->Run();
    BMessenger target(reporter);
    if (strcmp(command, "search") == 0) {
        StartLabelPrinterSearch(target);
    } else if (strcmp(command, "info") == 0 && argc >= 3) {
        reporter->fSingle = true;
        StartLabelPrinterIdentify(target, argv[2]);
    } else if (strcmp(command, "print") == 0 && argc >= 6) {
        StartLabelPrint(target, argv[2], Render(argv, argc, 3), 1, 3);
    } else
        return Usage();
    reporter->Wait();
    return reporter->fOk ? 0 : 1;
}
