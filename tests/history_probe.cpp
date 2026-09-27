#include "io/MarketData.h"
#include <iostream>
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    IO::CprTransport transport;
    IO::AlpacaOptions options{"test-key", "test-secret"};
    options.baseUrl = argv[1];
    IO::AlpacaClient client(options, transport);
    auto history = client.fetchHistory("AAPL", "2026-09-24", "2026-09-25");
    if (!history)
    {
        std::cerr << history.error().message;
        return 1;
    }
    std::cout << history->size();
    return history->size() == 2 ? 0 : 1;
}
