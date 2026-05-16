using UnityEngine;
using UnityEngine.Video;

public class CameraRaycastVideoControl : MonoBehaviour
{
    [Header("Configuration")] 
    public float raycastDistance = 30f;
    public float yOffset = 4f; // Raise the ray origin a little

    public float zoomSpeed = 3f;
    public float targetFOV = 15f;
    public float normalFOV = 60f;
    public float rotationAmount = 10f;
    public float rotationSpeed = 5f;

    [Header("Centering thresholds (fractions of screen width)")]
    public float centerThresholdEnter = 10f;

    // -------------------------------------------------
    // Private state
    // -------------------------------------------------
    private VideoPlayer lastVideoPlayer;
    private float originalPitch; // Camera pitch at start
    private Camera mainCamera;

    void Start()
    {
        mainCamera = gameObject.GetComponent<Camera>();
        originalPitch = mainCamera.transform.eulerAngles.x;
    }

    void Update()
    {
        Transform parent = transform.parent;
        if (!parent)
        {
            return;
        }

        Vector3 rayOrigin = parent.position + Vector3.up * yOffset;
        Ray ray = new(rayOrigin, parent.forward);

        if (Physics.Raycast(ray, out RaycastHit hitInfo, raycastDistance))
        {
            VideoPlayer hitVideoPlayer = hitInfo.collider.GetComponent<VideoPlayer>();
            if (hitVideoPlayer)
            {
                if (lastVideoPlayer != hitVideoPlayer)
                {
                    if (!hitVideoPlayer.isPlaying)
                    {
                        hitVideoPlayer.Play();
                    }

                    lastVideoPlayer = hitVideoPlayer;
                }
                
                Vector3 screenPos = mainCamera.WorldToScreenPoint(hitInfo.collider.gameObject.transform.position);
                float distanceFromCenter = Mathf.Abs(screenPos.x - Screen.width * 0.5f);

                // **Zoom and rotate regardless of pause state** – the video remains playing
                if (distanceFromCenter <= Screen.width * centerThresholdEnter)
                {
                    ZoomCameraTowardsQuad();
                    RotateCameraToFocusOnQuad();
                }
                else
                {
                    ResetCameraFOV();
                }
            }
        }
        else
        {
            // Ray hit nothing – pause everything
            PauseAllVideos();

            ResetCameraFOV();
            SmoothlyResetCameraRotation();
            lastVideoPlayer = null;
        }
    }

    // -------------------------------------------------
    // Helper: pause every VideoPlayer except the one we want active
    // -------------------------------------------------
    void PauseAllVideos()
    {
        // New API – no sorting, fastest mode
        VideoPlayer[] allPlayers = FindObjectsByType<VideoPlayer>();

        foreach (VideoPlayer vp in allPlayers)
        {
            if (vp.isPlaying)
            {
                vp.Pause();
            }
        }
    }

    // -------------------------------------------------
    // Zoom and rotation helpers (clamped)
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

    void RotateCameraToFocusOnQuad()
    {
        float targetPitch = originalPitch + rotationAmount;
        Quaternion targetRot = Quaternion.Euler(targetPitch, mainCamera.transform.eulerAngles.y,
            mainCamera.transform.eulerAngles.z);
        mainCamera.transform.rotation = Quaternion.Lerp(mainCamera.transform.rotation, targetRot,
            rotationSpeed * Time.deltaTime);
    }

    void SmoothlyResetCameraRotation()
    {
        Quaternion targetRot = Quaternion.Euler(originalPitch, mainCamera.transform.eulerAngles.y,
            mainCamera.transform.eulerAngles.z);
        mainCamera.transform.rotation = Quaternion.Lerp(mainCamera.transform.rotation, targetRot,
            rotationSpeed * Time.deltaTime);
    }
}