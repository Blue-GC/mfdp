#include <iostream>

#include "mfdp/mfdp.h"

int main(int argc, char** argv) {
  using namespace mfdp;
  try {
    if (argc < 2) {
      std::cerr << "usage: mfdp_example SOURCE_FILE [SYMBOLS]\n";
      return 1;
    }
    WriteOptions options;
    options.src = {argv[1]};
    options.kwrds = argc > 2 ? argv[2] : "*";
    options.output_dir = ".";
    WriteResult written = write(options);
    const std::string& artifactPath = written.outputs.at(0);
    std::cout << "wrote " << artifactPath << "\n" << formatInspect(inspect(artifactPath));
    ExtractOptions extractOptions;
    extractOptions.kwrds = options.kwrds;
    extractOptions.budget = 400;
    ExtractResult context = extract(artifactPath, extractOptions);
    std::cout << context.text;
    for (const auto& w : context.warnings) std::cout << "warning[" << w.code << "]: " << w.message << "\n";
    VerifyReport report = verify(artifactPath, {argv[1]});
    std::cout << formatVerify(report);
    requireFresh(report);
    return 0;
  } catch (const Error& e) {
    std::cerr << e.describe() << "\n";
    return 2;
  }
}
