using UnityEngine;
using UnityEngine.Video;

/// <summary>
/// Raycast is emitted from this GameObject’s parent.  
/// The video that is hit plays **and stays playing while the camera is zooming**.  
/// The video only stops when the ray no longer hits it.  
/// </summary>
public class CameraRaycastVideoControl : MonoBehaviour
{
    // -------------------------------------------------
    // 1️⃣  Camera / raycast configuration
    // -------------------------------------------------
    public Camera mainCamera;          // Camera that handles zoom/rotation
    public float raycastDistance = 10f;
    public float yOffset = 1f;         // Raise the ray origin a little

    // -------------------------------------------------
    // 2️⃣  Zoom / rotation (unchanged)
    // -------------------------------------------------
    public float zoomSpeed = 10f;
    public float targetFOV = 30f;
    public float normalFOV = 60f;
    public float rotationAmount = 5f;
    public float rotationSpeed = 5f;

    // -------------------------------------------------
    // 3️⃣  Centering thresholds (hysteresis)
    // -------------------------------------------------
    [Header("Centering thresholds (fractions of screen width)")]
    public float centerThresholdEnter = 0.08f;
    public float centerThresholdExit  = 0.12f;

    // -------------------------------------------------
    // 4️⃣  Private state
    // -------------------------------------------------
    private VideoPlayer currentVideoPlayer; // Video currently being shown
    private GameObject currentVideoObject;  // Its GameObject
    private bool isCentered = false;        // Is the quad inside the “focused” band?
    private float originalPitch;            // Camera pitch at start
    private Quaternion originalRotation;    // Full rotation at start

    void Start()
    {
        originalRotation = mainCamera.transform.rotation;
        originalPitch = mainCamera.transform.eulerAngles.x;
    }

    void Update()
    {
        // -------------------------------------------------
        // A️⃣  Build ray **from the parent object**
        // -------------------------------------------------
        Transform parent = transform.parent;
        if (parent == null)
        {
            Debug.LogWarning("CameraRaycastVideoControl needs a parent object to cast the ray from.");
            return;
        }

        Vector3 rayOrigin = parent.position + Vector3.up * yOffset;
        Ray ray = new Ray(rayOrigin, parent.forward);
        Debug.DrawRay(ray.origin, ray.direction * raycastDistance, Color.cyan);

        // -------------------------------------------------
        // B️⃣  Determine which video (if any) is hit
        // -------------------------------------------------
        VideoPlayer hitVideoPlayer = null;
        GameObject hitObject = null;

        if (Physics.Raycast(ray, out RaycastHit hitInfo, raycastDistance))
        {
            hitVideoPlayer = hitInfo.collider.GetComponent<VideoPlayer>();
            if (hitVideoPlayer != null)
                hitObject = hitInfo.collider.gameObject;
        }

        // -------------------------------------------------
        // C️⃣  Play the hit video and stop all others **only when the ray stops hitting**
        // -------------------------------------------------
        if (hitVideoPlayer != null)
        {
            // Pause every other video (if any)
            PauseAllVideosExcept(hitVideoPlayer);

            // Ensure the hit video is playing
            if (!hitVideoPlayer.isPlaying)
                hitVideoPlayer.Play();

            currentVideoPlayer = hitVideoPlayer;
            currentVideoObject = hitObject;
        }
        else
        {
            // Ray hit nothing – pause everything
            PauseAllVideosExcept(null);
            currentVideoPlayer = null;
            currentVideoObject = null;
        }

        // -------------------------------------------------
        // D️⃣  Center‑based zoom / rotation (video stays playing)
        // -------------------------------------------------
        if (currentVideoObject != null)
        {
            Vector3 screenPos = mainCamera.WorldToScreenPoint(currentVideoObject.transform.position);
            float distanceFromCenter = Mathf.Abs(screenPos.x - Screen.width * 0.5f);

            // Hysteresis: tighter entry, looser exit
            if (!isCentered && distanceFromCenter <= Screen.width * centerThresholdEnter)
                isCentered = true;
            else if (isCentered && distanceFromCenter > Screen.width * centerThresholdExit)
                isCentered = false;

            // **Zoom and rotate regardless of pause state** – the video remains playing
            if (isCentered)
            {
                ZoomCameraTowardsQuad();
                RotateCameraToFocusOnQuad();
            }
            else
            {
                ResetCameraFOV();
            }
        }
        else
        {
            ResetCameraFOV();
        }

        // Return rotation when nothing is being looked at
        if (currentVideoPlayer == null)
            SmoothlyResetCameraRotation();
    }

    // -------------------------------------------------
    // Helper: pause every VideoPlayer except the one we want active
    // -------------------------------------------------
    void PauseAllVideosExcept(VideoPlayer exception)
    {
        // New API – no sorting, fastest mode
        VideoPlayer[] allPlayers = FindObjectsByType<VideoPlayer>(FindObjectsSortMode.None);
        foreach (VideoPlayer vp in allPlayers)
        {
            if (vp != exception && vp.isPlaying)
                vp.Pause();
        }
    }

    // -------------------------------------------------
    // Zoom helpers (clamped)
    // -------------------------------------------------
    void ZoomCameraTowardsQuad()
    {
        float newFOV = Mathf.Lerp(mainCamera.fieldOfView, targetFOV, zoomSpeed * Time.deltaTime);
        mainCamera.fieldOfView = Mathf.Clamp(newFOV, targetFOV, normalFOV);
    }

    void ResetCameraFOV()
    {
        float newFOV = Mathf.Lerp(mainCamera.fieldOfView, normalFOV, zoomSpeed * Time.deltaTime);
        mainCamera.fieldOfView = Mathf.Clamp(newFOV, targetFOV, normalFOV);
    }

    // -------------------------------------------------
    // Rotation helpers
    // -------------------------------------------------
    void RotateCameraToFocusOnQuad()
    {
        float targetPitch = originalPitch + rotationAmount;
        Quaternion targetRot = Quaternion.Euler(targetPitch,
                                                mainCamera.transform.eulerAngles.y,
                                                mainCamera.transform.eulerAngles.z);
        mainCamera.transform.rotation = Quaternion.Lerp(mainCamera.transform.rotation,
                                                         targetRot,
                                                         rotationSpeed * Time.deltaTime);
    }

    void SmoothlyResetCameraRotation()
    {
        Quaternion targetRot = Quaternion.Euler(originalPitch,
                                                mainCamera.transform.eulerAngles.y,
                                                mainCamera.transform.eulerAngles.z);
        mainCamera.transform.rotation = Quaternion.Lerp(mainCamera.transform.rotation,
                                                         targetRot,
                                                         rotationSpeed * Time.deltaTime);
    }
}
