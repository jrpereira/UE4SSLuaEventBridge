#!/usr/bin/env bash
set -euo pipefail

c++ -std=c++20 -Wall -Wextra -Werror -Imod/include tests/DispatchBudgetTests.cpp -o /tmp/ue4ss-dispatch-budget-tests
/tmp/ue4ss-dispatch-budget-tests

c++ -std=c++20 -Wall -Wextra -Werror -pthread \
    -Imod/include \
    tests/SessionAliasIndexTests.cpp \
    -o /tmp/ue4ss-lua-event-bridge-tests

/tmp/ue4ss-lua-event-bridge-tests

c++ -std=c++20 -Wall -Wextra -Werror -Imod/include tests/BindingSnapshotTests.cpp -o /tmp/ue4ss-binding-snapshot-tests
/tmp/ue4ss-binding-snapshot-tests

c++ -std=c++20 -Wall -Wextra -Werror -Imod/include tests/LegacyInstallMigrationTests.cpp -o /tmp/ue4ss-legacy-install-migration-tests
/tmp/ue4ss-legacy-install-migration-tests

c++ -std=c++20 -Wall -Wextra -Werror -Imod/include tests/WeakObjectPtrTests.cpp -o /tmp/ue4ss-weak-object-tests
/tmp/ue4ss-weak-object-tests

c++ -std=c++20 -Wall -Wextra -Werror -pthread -Imod/include tests/QueueBuffersTests.cpp -o /tmp/ue4ss-queue-buffer-tests
/tmp/ue4ss-queue-buffer-tests

c++ -std=c++20 -Wall -Wextra -Werror -Imod/include tests/QueueDispatchScheduleTests.cpp -o /tmp/ue4ss-queue-schedule-tests
/tmp/ue4ss-queue-schedule-tests
