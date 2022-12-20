package com.datadoghq.profiler.alloc;

import com.datadoghq.profiler.AbstractProfilerTest;
import org.junit.jupiter.api.Disabled;
import org.junit.jupiter.api.Test;
import org.openjdk.jmc.common.item.Aggregators;
import org.openjdk.jmc.common.item.IItemCollection;
import org.openjdk.jmc.common.item.ItemFilters;
import org.openjdk.jol.info.GraphLayout;

import java.util.Random;
import java.util.concurrent.atomic.AtomicLong;

import static org.junit.jupiter.api.Assertions.assertTrue;

@Disabled("flaky")
public class AllocationProfilerTest extends AbstractProfilerTest {


  @Test
  public void shouldGetObjectAllocationInNewTLABSamples() throws InterruptedException {
    AllocatingTarget target1 = new AllocatingTarget();
    AllocatingTarget target2 = new AllocatingTarget();
    runTests(target1, target2);
    IItemCollection allocations = verifyEvents("datadog.ObjectAllocationInNewTLAB");
    // FIXME when more tests are ported to this structure
    //     assertAllocations(allocations, int[].class, target1, target2);
    //     assertAllocations(allocations, Integer[].class, target1, target2);
  }

  private static void assertAllocations(IItemCollection allocations, Class<?> clazz, AllocatingTarget... targets) {
    long allocated = 0;
    for (AllocatingTarget target : targets) {
      allocated += target.getAllocated(clazz);
    }
    IItemCollection allocationsByType = allocations.apply(ItemFilters.equals(TYPE, clazz.getCanonicalName()));
    assertTrue(allocationsByType.hasItems());
    long recorded = allocationsByType.getAggregate(Aggregators.sum(SIZE)).longValue();
    long absoluteError = Math.abs(recorded - allocated);
    assertTrue(absoluteError < allocated / 10,
        String.format("allocation samples should be within 10pct tolerance of allocated memory (recorded %d, allocated %d)",
            recorded, allocated));
  }

  @Override
  protected String getProfilerCommand() {
    return "alloc";
  }


  public static class AllocatingTarget extends ClassValue<AtomicLong> implements Runnable {
    public static volatile Object sink;

    @Override
    public void run() {
      Random random = new Random(0);
      for (int i = 0; i < 1_000_000; i++) {
        allocate(random);
      }
    }

    public long getAllocated(Class<?> clazz) {
      return get(clazz).get();
    }

    private void allocate(Random random) {
      Object object;
      if (random.nextBoolean()) {
        object = new int[128 * 1000];
      } else {
        object = new Integer[128 * 1000];
      }
      get(object.getClass()).addAndGet(GraphLayout.parseInstance(object).totalSize());
      sink = object;
    }

    @Override
    protected AtomicLong computeValue(Class<?> type) {
      return new AtomicLong();
    }
  }
}
