;;; emacs-web-native-smoke.el --- Local graphical build smoke test -*- lexical-binding: t; -*-

;; Run in a fresh graphical Emacs, not --batch.  See NATIVE-BUILD.md.
;; This checks the build/ABI only, not complete renderer fidelity.

;; The HTML consumer normally registers this condition before calling the ABI.
(define-error 'emacs-web-display-not-ready "Native display is stale")

(run-with-timer
 1 nil
 (lambda ()
   (condition-case err
       (progn
         (unless (and (display-graphic-p) (eq window-system 'x)
                      (featurep 'cairo)
                      (= (emacs-web-native-display 'version) 1))
           (error "X11/Cairo/native ABI 1 unavailable"))
         (switch-to-buffer (get-buffer-create "*native-build-smoke*"))
         (erase-buffer)
         (insert "Native ABI 1: Latin, العربية, עברית, é\n")
         (redisplay t)
         (let ((packet (emacs-web-native-display (selected-frame))))
           (unless (and (vectorp packet) (> (length packet) 0))
             (error "No completed native windows"))
           (garbage-collect)
           (unless (window-live-p (aref (aref packet 0) 0))
             (error "Packet did not retain its window"))
           (insert "Stale matrix check\n")
           (unless (condition-case nil
                       (progn (emacs-web-native-display (selected-frame)) nil)
                     (emacs-web-display-not-ready t))
             (error "Stale matrix was not rejected"))
           (redisplay t)
           (emacs-web-native-display (selected-frame))
           (with-temp-file (getenv "EMACS_WEB_NATIVE_SMOKE_REPORT")
             (insert (format "PASS: X11/Cairo ABI 1; %d windows; packet retained across GC; stale matrix rejected; recapture succeeded\nFeatures: %s\nImages: %S\n"
                             (length packet) system-configuration-features
                             (mapcar (lambda (type)
                                       (cons type (image-type-available-p type)))
                                     '(png jpeg gif tiff svg webp xpm))))))
         (kill-emacs 0))
     (error
      (with-temp-file (getenv "EMACS_WEB_NATIVE_SMOKE_REPORT")
        (insert (format "FAIL: %S\n" err)))
      (kill-emacs 1)))))
