#include "presence.hpp"
#include <cassert>

int main() {
    using mgs::presence::redirect_target;
    assert(redirect_target("title", "title=st20") == "st20");
    assert(redirect_target("title", "title=st11_boss") == "st11_boss");
    assert(redirect_target("title", "title=title").empty());
    assert(redirect_target("init", "title=st11_boss").empty());
    for (auto rule : {"title", "title=", "title=../st11_boss", "title=st11/boss", "title=st11=boss",
                      "title=abcdefghijklmnopqrstuvwxyz123456"})
        assert(redirect_target("title", rule).empty());
}
