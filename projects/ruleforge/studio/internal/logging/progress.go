package logging

import (
	"context"
	"time"
)

// Phase reports start, elapsed heartbeats and completion, joining its heartbeat
// goroutine before returning so messages cannot spill into the next phase.
func Phase(ctx context.Context, logf func(string, ...any), name string, action func() error) error {
	if logf == nil {
		return action()
	}
	started := time.Now()
	logf("%s started", name)
	stop := make(chan struct{})
	done := make(chan struct{})
	go func() {
		defer close(done)
		ticker := time.NewTicker(5 * time.Second)
		defer ticker.Stop()
		for {
			select {
			case <-stop:
				return
			case <-ctx.Done():
				return
			case <-ticker.C:
				logf("%s running (%.1fs elapsed)", name, time.Since(started).Seconds())
			}
		}
	}()
	err := action()
	close(stop)
	<-done
	if err != nil {
		logf("%s failed after %.3fs: %v", name, time.Since(started).Seconds(), err)
	} else {
		logf("%s finished in %.3fs", name, time.Since(started).Seconds())
	}
	return err
}
