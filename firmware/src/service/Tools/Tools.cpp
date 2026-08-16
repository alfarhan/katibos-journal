#include "Tools.h"
#include "app/app.h"

String formatNumber(int num)
{
    String formattedNumber = "";
    int digitCount = 0;
    if (num < 0)
    {
        formattedNumber += "-";
        num = -num;
    }
    do
    {
        if (digitCount > 0 && digitCount % 3 == 0)
        {
            formattedNumber = "," + formattedNumber;
        }
        formattedNumber = String(num % 10) + formattedNumber;
        num /= 10;
        digitCount++;
    } while (num > 0);

    return formattedNumber;
}

// Get the size of a file in bytes
size_t fileSize(String fileName)
{
    size_t file_size = 0;
    if (gfs()->exists(fileName.c_str()))
    {
        File file = gfs()->open(fileName.c_str(), "r");
        if (!file)
        { // something bad happened
            char buffer[32];
            sprintf(buffer, "Failed to open a file. %s\n", fileName);
            _log(buffer);
            file_size = -1;
        }
        else
        { // file exists
            file_size = file.size();
        }
        //
        file.close();
        delay(100);
    }
    return file_size;
}

String format(const char *format, ...)
{
    char buffer[256]; // Adjust the size according to your needs
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return String(buffer);
}
