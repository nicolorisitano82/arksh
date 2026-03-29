# Homebrew formula for arksh
#
# Usage:
#   brew install nicolorisitano82/arksh/arksh
#
# To create a personal tap:
#   brew tap nicolorisitano82/arksh https://github.com/nicolorisitano82/homebrew-arksh
#   brew install arksh
#
# Before a tagged release exists, install from HEAD:
#   brew install --HEAD nicolorisitano82/arksh/arksh

class Arksh < Formula
  desc "ARKsh — Archetype Shell: typed object-aware shell"
  homepage "https://github.com/nicolorisitano82/arksh"
  license "MIT"

  # ── Stable release (update URL and sha256 after each tagged release) ────
  # url "https://github.com/nicolorisitano82/arksh/archive/refs/tags/v0.9.0.tar.gz"
  # sha256 "REPLACE_WITH_SHA256_OF_THE_RELEASE_TARBALL"
  # version "0.9.0"

  # ── HEAD (build from latest main branch) ────────────────────────────────
  head "https://github.com/nicolorisitano82/arksh.git", branch: "main"

  depends_on "cmake" => :build

  def install
    system "cmake", "-S", ".", "-B", "build",
           "-DCMAKE_BUILD_TYPE=Release",
           "-DBUILD_TESTING=OFF",
           *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  def post_install
    # Create the plugin directory expected by the runtime
    (var/"arksh/plugins").mkpath
  end

  test do
    assert_equal "directory", shell_output("#{bin}/arksh -c '. -> type'").strip
    assert_equal "0", shell_output("#{bin}/arksh --sh -c 'echo $?'").strip
  end
end
